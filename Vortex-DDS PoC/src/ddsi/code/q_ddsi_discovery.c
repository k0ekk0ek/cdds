/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to TO_YEAR PrismTech
 *   Limited and its licensees. All rights reserved. See file:
 *
 *                   $OSPL_HOME/LICENSE
 *
 *   for full copyright notice and license terms.
 *
 */
#include <ctype.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "os.h"

#include "ut_avl.h"
#include "ddsi_ser.h"
#include "q_protocol.h"
#include "q_rtps.h"
#include "q_misc.h"
#include "q_config.h"
#include "q_log.h"
#include "q_plist.h"
#include "q_unused.h"
#include "q_xevent.h"
#include "q_addrset.h"
#include "q_ddsi_discovery.h"
#include "q_radmin.h"
#include "q_ephash.h"
#include "q_entity.h"
#include "q_globals.h"
#include "q_xmsg.h"
#include "q_bswap.h"
#include "q_transmit.h"
#include "q_lease.h"
#include "q_error.h"
#include "q_builtin_topic.h"
#include "q_osplser.h"
#include "q_md5.h"
#include "q_feature_check.h"

#include "sysdeps.h"

static const nn_vendorid_t ownvendorid = MY_VENDOR_ID;

static int get_locator (nn_locator_t *loc, const nn_locators_t *locs, int uc_same_subnet)
{
  struct nn_locators_one *l;
  nn_locator_t first, samenet;
  int first_set = 0, samenet_set = 0;
  memset (&first, 0, sizeof (first));
  memset (&samenet, 0, sizeof (samenet));

  /* Preferably an (the first) address that matches a network we are
     on; if none does, pick the first. No multicast locator ever will
     match, so the first one will be used. */
  for (l = locs->first; l != NULL; l = l->next)
  {
    os_sockaddr_storage tmp;
    int i;

    /* Skip locators of the wrong kind */

    if (! ddsi_factory_supports (gv.m_factory, l->loc.kind))
    {
      continue;
    }

    nn_loc_to_address (&tmp, &l->loc);

    if (l->loc.kind == NN_LOCATOR_KIND_UDPv4 && gv.extmask.s_addr != 0)
    {
      /* If the examined locator is in the same subnet as our own
         external IP address, this locator will be translated into one
         in the same subnet as our own local ip and selected. */
      os_sockaddr_in *tmp4 = (os_sockaddr_in *) &tmp;
      const os_sockaddr_in *ownip = (os_sockaddr_in *) &gv.ownip;
      const os_sockaddr_in *extip = (os_sockaddr_in *) &gv.extip;

      if ((tmp4->sin_addr.s_addr & gv.extmask.s_addr) == (extip->sin_addr.s_addr & gv.extmask.s_addr))
      {
        /* translate network part of the IP address from the external
           one to the internal one */
        tmp4->sin_addr.s_addr =
          (tmp4->sin_addr.s_addr & ~gv.extmask.s_addr) | (ownip->sin_addr.s_addr & gv.extmask.s_addr);
        nn_address_to_loc (loc, &tmp, l->loc.kind);
        return 1;
      }
    }

#if OS_SOCKET_HAS_IPV6
    if ((l->loc.kind == NN_LOCATOR_KIND_UDPv6) || (l->loc.kind == NN_LOCATOR_KIND_TCPv6))
    {
      /* We (cowardly) refuse to accept advertised link-local
         addresses unles we're in "link-local" mode ourselves.  Then
         we just hope for the best.  */
      if (!gv.ipv6_link_local && IN6_IS_ADDR_LINKLOCAL (&((os_sockaddr_in6 *) &tmp)->sin6_addr))
        continue;
    }
#endif

    if (!first_set)
    {
      first = l->loc;
      first_set = 1;
    }
    for (i = 0; i < gv.n_interfaces; i++)
    {
      if (os_sockaddrSameSubnet ((os_sockaddr *) &tmp, (os_sockaddr *) &gv.interfaces[i].addr, (os_sockaddr *) &gv.interfaces[i].netmask))
      {
        if (os_sockaddrIPAddressEqual ((os_sockaddr*) &gv.interfaces[i].addr, (os_sockaddr*) &gv.ownip))
        {
          /* matches the preferred interface -> the very best situation */
          *loc = l->loc;
          return 1;
        }
        else if (!samenet_set)
        {
          /* on a network we're connected to */
          samenet = l->loc;
          samenet_set = 1;
        }
      }
    }
  }
  if (!uc_same_subnet)
  {
    if (samenet_set)
    {
      /* prefer a directly connected network */
      *loc = samenet;
      return 1;
    }
    else if (first_set)
    {
      /* else any address we found will have to do */
      *loc = first;
      return 1;
    }
  }
  return 0;
}

/******************************************************************************
 ***
 *** SPDP
 ***
 *****************************************************************************/

static void maybe_add_pp_as_meta_to_as_disc (const struct addrset *as_meta)
{
  if (addrset_empty_mc (as_meta))
  {
    nn_locator_t loc;
    if (addrset_any_uc (as_meta, &loc))
    {
      add_to_addrset (gv.as_disc, &loc);
    }
  }
}

int spdp_write (struct participant *pp)
{
  static const nn_vendorid_t myvendorid = MY_VENDOR_ID;
  serdata_t serdata;
  serstate_t serstate;
  struct nn_xmsg *mpayload;
  size_t payload_sz;
  char *payload_blob;
  struct nn_locators_one def_uni_loc_one, def_multi_loc_one, meta_uni_loc_one, meta_multi_loc_one;
  nn_plist_t ps;
  nn_guid_t kh;
  struct writer *wr;
  size_t size;
  char node[64];
  uint64_t qosdiff;

  TRACE (("spdp_write(%x:%x:%x:%x)\n", PGUID (pp->e.guid)));

  if ((wr = get_builtin_writer (pp, NN_ENTITYID_SPDP_BUILTIN_PARTICIPANT_WRITER)) == NULL)
  {
    TRACE (("spdp_write(%x:%x:%x:%x) - builtin participant writer not found\n", PGUID (pp->e.guid)));
    return 0;
  }

  /* First create a fake message for the payload: we can add plists to
     xmsgs easily, but not to serdata.  But it is rather easy to copy
     the payload of an xmsg over to a serdata ...  Expected size isn't
     terribly important, the msg will grow as needed, address space is
     essentially meaningless because we only use the message to
     construct the payload. */
  mpayload = nn_xmsg_new (gv.xmsgpool, &pp->e.guid.prefix, 0, NN_XMSG_KIND_DATA);

  nn_plist_init_empty (&ps);
  ps.present |= PP_PARTICIPANT_GUID | PP_BUILTIN_ENDPOINT_SET |
    PP_PROTOCOL_VERSION | PP_VENDORID | PP_PARTICIPANT_LEASE_DURATION;
  ps.participant_guid = pp->e.guid;
  ps.builtin_endpoint_set = pp->bes;
  ps.protocol_version.major = RTPS_MAJOR;
  ps.protocol_version.minor = RTPS_MINOR;
  ps.vendorid = myvendorid;
  if (pp->prismtech_bes)
  {
    ps.present |= PP_PRISMTECH_BUILTIN_ENDPOINT_SET;
    ps.prismtech_builtin_endpoint_set = pp->prismtech_bes;
  }
  ps.default_unicast_locators.n = 1;
  ps.default_unicast_locators.first =
    ps.default_unicast_locators.last = &def_uni_loc_one;
  ps.metatraffic_unicast_locators.n = 1;
  ps.metatraffic_unicast_locators.first =
    ps.metatraffic_unicast_locators.last = &meta_uni_loc_one;
  def_uni_loc_one.next = NULL;
  meta_uni_loc_one.next = NULL;

  if (config.many_sockets_mode)
  {
    def_uni_loc_one.loc = pp->m_locator;
    meta_uni_loc_one.loc = pp->m_locator;
  }
  else
  {
    def_uni_loc_one.loc = gv.loc_default_uc;
    meta_uni_loc_one.loc = gv.loc_meta_uc;
  }

  if (config.publish_uc_locators)
  {
    ps.present |= PP_DEFAULT_UNICAST_LOCATOR | PP_METATRAFFIC_UNICAST_LOCATOR;
    ps.aliased |= PP_DEFAULT_UNICAST_LOCATOR | PP_METATRAFFIC_UNICAST_LOCATOR;
  }

  if (config.allowMulticast)
  {
    int include = 0;
#ifdef DDSI_INCLUDE_SSM
    /* Note that if the default multicast address is an SSM address,
       we will simply advertise it. The recipients better understand
       it means the writers will publish to address and the readers
       favour SSM. */
    if (is_ssm_mcaddr (&gv.loc_default_mc))
      include = (config.allowMulticast & AMC_SSM) != 0;
    else
      include = (config.allowMulticast & AMC_ASM) != 0;
#else
    if (config.allowMulticast & AMC_ASM)
      include = 1;
#endif
    if (include)
    {
      ps.present |= PP_DEFAULT_MULTICAST_LOCATOR | PP_METATRAFFIC_MULTICAST_LOCATOR;
      ps.aliased |= PP_DEFAULT_MULTICAST_LOCATOR | PP_METATRAFFIC_MULTICAST_LOCATOR;
      ps.default_multicast_locators.n = 1;
      ps.default_multicast_locators.first =
      ps.default_multicast_locators.last = &def_multi_loc_one;
      ps.metatraffic_multicast_locators.n = 1;
      ps.metatraffic_multicast_locators.first =
      ps.metatraffic_multicast_locators.last = &meta_multi_loc_one;
      def_multi_loc_one.next = NULL;
      def_multi_loc_one.loc = gv.loc_default_mc;
      meta_multi_loc_one.next = NULL;
      meta_multi_loc_one.loc = gv.loc_meta_mc;
    }
  }
  ps.participant_lease_duration = nn_to_ddsi_duration (pp->lease_duration);

  /* Add PrismTech specific version information */
  {
    ps.present |= PP_PRISMTECH_PARTICIPANT_VERSION_INFO;
    ps.prismtech_participant_version_info.version = 0;
    ps.prismtech_participant_version_info.flags =
      NN_PRISMTECH_FL_DDSI2_PARTICIPANT_FLAG |
      NN_PRISMTECH_FL_PTBES_FIXED_0;
#if !LITE
    ps.prismtech_participant_version_info.flags |=
      NN_PRISMTECH_FL_DISCOVERY_INCLUDES_GID |
      NN_PRISMTECH_FL_KERNEL_SEQUENCE_NUMBER;
#endif
    os_mutexLock (&gv.privileged_pp_lock);
    if (pp->is_ddsi2_pp)
      ps.prismtech_participant_version_info.flags |= NN_PRISMTECH_FL_PARTICIPANT_IS_DDSI2;
    os_mutexUnlock (&gv.privileged_pp_lock);

    os_gethostname(node, sizeof(node)-1);
    node[sizeof(node)-1] = '\0';
#if LITE
    size = strlen(node) + strlen(OSPL_VERSION_STR) + strlen(OSPL_HOST_STR) + strlen(OSPL_TARGET_STR) + 4; /* + ///'\0' */
#else
    size = strlen(node) + strlen(OSPL_VERSION_STR) + strlen(OSPL_INNER_REV_STR) +
            strlen(OSPL_OUTER_REV_STR) + strlen(OSPL_HOST_STR) + strlen(OSPL_TARGET_STR) + 6; /* + /////'\0' */
#endif
    ps.prismtech_participant_version_info.internals = os_malloc(size);
#if LITE
    snprintf(ps.prismtech_participant_version_info.internals, size, "%s/%s/%s/%s",
             node, OSPL_VERSION_STR, OSPL_HOST_STR, OSPL_TARGET_STR);
#else
    snprintf(ps.prismtech_participant_version_info.internals, size, "%s/%s/%s/%s/%s/%s",
             node, OSPL_VERSION_STR, OSPL_INNER_REV_STR, OSPL_OUTER_REV_STR, OSPL_HOST_STR, OSPL_TARGET_STR);
#endif
    TRACE (("spdp_write(%x:%x:%x:%x) - internals: %s\n", PGUID (pp->e.guid), ps.prismtech_participant_version_info.internals));
  }

  /* Participant QoS's insofar as they are set, different from the default, and mapped to the SPDP data, rather than to the PrismTech-specific CMParticipant endpoint.  Currently, that means just USER_DATA. */
  qosdiff = nn_xqos_delta (&pp->plist->qos, &gv.default_plist_pp.qos, QP_USER_DATA);
  if (config.explicitly_publish_qos_set_to_default)
    qosdiff |= ~QP_UNRECOGNIZED_INCOMPATIBLE_MASK;

  assert (ps.qos.present == 0);
  nn_plist_addtomsg (mpayload, &ps, ~(uint64_t)0, 0);
  nn_plist_addtomsg (mpayload, pp->plist, 0, qosdiff);
  nn_xmsg_addpar_sentinel (mpayload);

  /* A NULL topic implies a parameter list, now that we do PMD through
     the serializer */
  serstate = ddsi_serstate_new (gv.serpool, NULL);
  payload_blob = nn_xmsg_payload (&payload_sz, mpayload);
  ddsi_serstate_append_blob (serstate, 4, payload_sz, payload_blob);
  kh = nn_hton_guid (pp->e.guid);
  serstate_set_key (serstate, 0, &kh);
  ddsi_serstate_set_msginfo (serstate, 0, now (), NULL);
  serdata = ddsi_serstate_fix (serstate);
  nn_plist_fini(&ps);
  nn_xmsg_free (mpayload);

  return write_sample (NULL, wr, serdata);
}

int spdp_dispose_unregister (struct participant *pp)
{
  struct nn_xmsg *mpayload;
  size_t payload_sz;
  char *payload_blob;
  nn_plist_t ps;
  serdata_t serdata;
  serstate_t serstate;
  nn_guid_t kh;
  struct writer *wr;

  if ((wr = get_builtin_writer (pp, NN_ENTITYID_SPDP_BUILTIN_PARTICIPANT_WRITER)) == NULL)
  {
    TRACE (("spdp_dispose_unregister(%x:%x:%x:%x) - builtin participant writer not found\n", PGUID (pp->e.guid)));
    return 0;
  }

  mpayload = nn_xmsg_new (gv.xmsgpool, &pp->e.guid.prefix, 0, NN_XMSG_KIND_DATA);
  nn_plist_init_empty (&ps);
  ps.present |= PP_PARTICIPANT_GUID;
  ps.participant_guid = pp->e.guid;
  nn_plist_addtomsg (mpayload, &ps, ~(uint64_t)0, ~(uint64_t)0);
  nn_xmsg_addpar_sentinel (mpayload);

  serstate = ddsi_serstate_new (gv.serpool, NULL);
  payload_blob = nn_xmsg_payload (&payload_sz, mpayload);
  ddsi_serstate_append_blob (serstate, 4, payload_sz, payload_blob);
  kh = nn_hton_guid (pp->e.guid);
  serstate_set_key (serstate, 1, &kh);
  ddsi_serstate_set_msginfo (serstate, NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER, now (), NULL);
  serdata = ddsi_serstate_fix (serstate);
  nn_xmsg_free (mpayload);

  return write_sample (NULL, wr, serdata);
}

static unsigned pseudo_random_delay (const nn_guid_t *x, const nn_guid_t *y, nn_mtime_t tnow)
{
  /* You know, an ordinary random generator would be even better, but
     the C library doesn't have a reentrant one and I don't feel like
     integrating, say, the Mersenne Twister right now. */
#define UINT64_CONST(x, y, z) (((uint64_t) (x) * 1000000 + (y)) * 1000000 + (z))
  static const uint64_t cs[] = {
    UINT64_CONST (15385148,  50874, 689571),
    UINT64_CONST (17503036, 526311, 582379),
    UINT64_CONST (11075621, 958654, 396447),
    UINT64_CONST ( 9748227, 842331,  24047),
    UINT64_CONST (14689485, 562394, 710107),
    UINT64_CONST (17256284, 993973, 210745),
    UINT64_CONST ( 9288286, 355086, 959209),
    UINT64_CONST (17718429, 552426, 935775),
    UINT64_CONST (10054290, 541876, 311021),
    UINT64_CONST (13417933, 704571, 658407)
  };
#undef UINT64_CONST
  uint32_t a = x->prefix.u[0], b = x->prefix.u[1], c = x->prefix.u[2], d = x->entityid.u;
  uint32_t e = y->prefix.u[0], f = y->prefix.u[1], g = y->prefix.u[2], h = y->entityid.u;
  uint32_t i = (uint32_t) ((uint64_t) tnow.v >> 32), j = (uint32_t) tnow.v;
  uint64_t m = 0;
  m += (a + cs[0]) * (b + cs[1]);
  m += (c + cs[2]) * (d + cs[3]);
  m += (e + cs[4]) * (f + cs[5]);
  m += (g + cs[6]) * (h + cs[7]);
  m += (i + cs[8]) * (j + cs[9]);
  return (unsigned) (m >> 32);
}

static void respond_to_spdp (const nn_guid_t *dest_proxypp_guid)
{
  struct ephash_enum_participant est;
  struct participant *pp;
  nn_mtime_t tnow = now_mt ();
  ephash_enum_participant_init (&est);
  while ((pp = ephash_enum_participant_next (&est)) != NULL)
  {
    /* delay_base has 32 bits, so delay_norm is approximately 1s max;
       delay_max <= 1s by config checks */
    unsigned delay_base = pseudo_random_delay (&pp->e.guid, dest_proxypp_guid, tnow);
    unsigned delay_norm = delay_base >> 2;
    int64_t delay_max_ms = config.spdp_response_delay_max / 1000000;
    int64_t delay = (int64_t) delay_norm * delay_max_ms / 1000;
    nn_mtime_t tsched = add_duration_to_mtime (tnow, delay);
    TRACE ((" %"PRId64, delay));
    if (!config.unicast_response_to_spdp_messages)
      /* pp can't reach gc_delete_participant => can safely reschedule */
      resched_xevent_if_earlier (pp->spdp_xevent, tsched);
    else
      qxev_spdp (tsched, &pp->e.guid, dest_proxypp_guid);
  }
  ephash_enum_participant_fini (&est);
}

static void handle_SPDP_dead (const struct receiver_state *rst, const nn_plist_t *datap)
{
  nn_guid_t guid;

  if (datap->present & PP_PARTICIPANT_GUID)
  {
    guid = datap->participant_guid;
    TRACE ((" %x:%x:%x:%x", PGUID (guid)));
    assert (guid.entityid.u == NN_ENTITYID_PARTICIPANT);
    if (delete_proxy_participant_by_guid (&guid, 0) < 0)
    {
      TRACE ((" unknown"));
    }
    else
    {
      TRACE ((" delete"));
    }
  }
  else
  {
    NN_WARNING2 ("data (SPDP, vendor %d.%d): no/invalid payload\n", rst->vendor.id[0], rst->vendor.id[1]);
  }
}

static void allowmulticast_aware_add_to_addrset (struct addrset *as, const nn_locator_t *loc)
{
#if DDSI_INCLUDE_SSM
  if (is_ssm_mcaddr (loc))
  {
    if (!(config.allowMulticast & AMC_SSM))
      return;
  }
  else if (is_mcaddr (loc))
  {
    if (!(config.allowMulticast & AMC_ASM))
      return;
  }
#else
  if (is_mcaddr (loc) && !(config.allowMulticast & AMC_ASM))
    return;
#endif
  add_to_addrset (as, loc);
}

static void handle_SPDP_alive (const struct receiver_state *rst, const nn_plist_t *datap)
{
  const unsigned bes_sedp_announcer_mask =
    NN_DISC_BUILTIN_ENDPOINT_SUBSCRIPTION_ANNOUNCER |
    NN_DISC_BUILTIN_ENDPOINT_PUBLICATION_ANNOUNCER;
  struct addrset *as_meta, *as_default;
  struct proxy_participant *proxypp;
  unsigned builtin_endpoint_set;
  unsigned prismtech_builtin_endpoint_set;
  nn_guid_t privileged_pp_guid;
  nn_duration_t lease_duration;
  unsigned custom_flags = 0;

  if (!(datap->present & PP_PARTICIPANT_GUID) || !(datap->present & PP_BUILTIN_ENDPOINT_SET))
  {
    NN_WARNING2 ("data (SPDP, vendor %d.%d): no/invalid payload\n", rst->vendor.id[0], rst->vendor.id[1]);
    return;
  }

  /* At some point the RTI implementation didn't mention
     BUILTIN_ENDPOINT_PARTICIPANT_MESSAGE_DATA_READER & ...WRITER, or
     so it seemed; and yet they are necessary for correct operation,
     so add them. */
  builtin_endpoint_set = datap->builtin_endpoint_set;
  prismtech_builtin_endpoint_set = (datap->present & PP_PRISMTECH_BUILTIN_ENDPOINT_SET) ? datap->prismtech_builtin_endpoint_set : 0;
  if (vendor_is_rti (rst->vendor) &&
      ((builtin_endpoint_set &
        (NN_BUILTIN_ENDPOINT_PARTICIPANT_MESSAGE_DATA_READER |
         NN_BUILTIN_ENDPOINT_PARTICIPANT_MESSAGE_DATA_WRITER))
       != (NN_BUILTIN_ENDPOINT_PARTICIPANT_MESSAGE_DATA_READER |
           NN_BUILTIN_ENDPOINT_PARTICIPANT_MESSAGE_DATA_WRITER)) &&
      config.assume_rti_has_pmd_endpoints)
  {
    NN_WARNING2 ("data (SPDP, vendor %d.%d): assuming unadvertised PMD endpoints do exist\n",
                 rst->vendor.id[0], rst->vendor.id[1]);
    builtin_endpoint_set |=
      NN_BUILTIN_ENDPOINT_PARTICIPANT_MESSAGE_DATA_READER |
      NN_BUILTIN_ENDPOINT_PARTICIPANT_MESSAGE_DATA_WRITER;
  }
  if ((datap->present & PP_PRISMTECH_PARTICIPANT_VERSION_INFO) &&
      (datap->present & PP_PRISMTECH_BUILTIN_ENDPOINT_SET) &&
      !(datap->prismtech_participant_version_info.flags & NN_PRISMTECH_FL_PTBES_FIXED_0))
  {
    /* FIXED_0 (bug 0) indicates that this is an updated version that advertises
       CM readers/writers correctly (without it, we could make a reasonable guess,
       but it would cause problems with cases where we would be happy with only
       (say) CM participant. Have to do a backwards-compatible fix because it has
       already been released with the flags all aliased to bits 0 and 1 ... */
      TRACE ((" (ptbes_fixed_0 %x)", prismtech_builtin_endpoint_set));
      if (prismtech_builtin_endpoint_set & NN_DISC_BUILTIN_ENDPOINT_CM_PARTICIPANT_READER)
        prismtech_builtin_endpoint_set |= NN_DISC_BUILTIN_ENDPOINT_CM_PUBLISHER_READER | NN_DISC_BUILTIN_ENDPOINT_CM_SUBSCRIBER_READER;
      if (prismtech_builtin_endpoint_set & NN_DISC_BUILTIN_ENDPOINT_CM_PARTICIPANT_WRITER)
        prismtech_builtin_endpoint_set |= NN_DISC_BUILTIN_ENDPOINT_CM_PUBLISHER_WRITER | NN_DISC_BUILTIN_ENDPOINT_CM_SUBSCRIBER_WRITER;
  }

  TRACE ((" %x:%x:%x:%x", PGUID (datap->participant_guid)));

  /* Local SPDP packets may be looped back, and that may include ones
     currently being deleted.  The first thing that happens when
     deleting a participant is removing it from the hash table, and
     consequently the looped back packet may appear to be from an
     unknown participant.  So we handle that, too. */

  if (is_deleted_participant_guid (&datap->participant_guid, DPG_REMOTE))
  {
    TRACE ((" (recently deleted)"));
    return;
  }

#if LITE
  if (!config.enableLoopback)
#endif
  {
    int islocal = 0;
    if (ephash_lookup_participant_guid (&datap->participant_guid))
      islocal = 1;
#if ! LITE
    if (!islocal && is_own_vendor (datap->vendorid))
    {
      os_mutexLock (&gv.privileged_pp_lock);
      if (gv.privileged_pp && datap->participant_guid.prefix.u[0] == gv.privileged_pp->e.guid.prefix.u[0])
        islocal = 2;
      os_mutexUnlock (&gv.privileged_pp_lock);
    }
#endif
    if (islocal)
    {
      TRACE ((" (local %d)", islocal));
      return;
    }
  }

  if ((proxypp = ephash_lookup_proxy_participant_guid (&datap->participant_guid)) != NULL)
  {
    /* SPDP processing is so different from normal processing that we
       are even skipping the automatic lease renewal.  Therefore do it
       regardless of
       config.arrival_of_data_asserts_pp_and_ep_liveliness. */
    TRACE ((" (known)"));
    lease_renew (proxypp->lease, now ());
    return;
  }

  TRACE ((" bes %x ptbes %x NEW", builtin_endpoint_set, prismtech_builtin_endpoint_set));

  if (datap->present & PP_PARTICIPANT_LEASE_DURATION)
  {
    lease_duration = datap->participant_lease_duration;
  }
  else
  {
    TRACE ((" (PARTICIPANT_LEASE_DURATION defaulting to 100s)"));
    lease_duration = nn_to_ddsi_duration (100 * T_SECOND);
  }

  /* If any of the SEDP announcer are missing AND the guid prefix of
     the SPDP writer differs from the guid prefix of the new participant,
     we make it dependent on the writer's participant.  See also the
     lease expiration handling.  Note that the entityid MUST be
     NN_ENTITYID_PARTICIPANT or ephash_lookup will assert.  So we only
     zero the prefix. */
  privileged_pp_guid.prefix = rst->src_guid_prefix;
  privileged_pp_guid.entityid.u = NN_ENTITYID_PARTICIPANT;
  if ((builtin_endpoint_set & bes_sedp_announcer_mask) != bes_sedp_announcer_mask &&
      memcmp (&privileged_pp_guid, &datap->participant_guid, sizeof (nn_guid_t)) != 0)
    TRACE ((" (depends on %x:%x:%x:%x)", PGUID (privileged_pp_guid)));
  else
    memset (&privileged_pp_guid.prefix, 0, sizeof (privileged_pp_guid.prefix));

#if !LITE
  if ((datap->present & PP_PRISMTECH_SERVICE_TYPE) &&
      (datap->service_type == (unsigned) V_SERVICETYPE_DDSI2 ||
       datap->service_type == (unsigned) V_SERVICETYPE_DDSI2E))
    custom_flags |= CF_PARTICIPANT_IS_DDSI2;
#endif

  if (datap->present & PP_PRISMTECH_PARTICIPANT_VERSION_INFO) {
    if (datap->prismtech_participant_version_info.flags & NN_PRISMTECH_FL_KERNEL_SEQUENCE_NUMBER)
      custom_flags |= CF_INC_KERNEL_SEQUENCE_NUMBERS;

    if ((datap->prismtech_participant_version_info.flags & NN_PRISMTECH_FL_DDSI2_PARTICIPANT_FLAG) &&
        (datap->prismtech_participant_version_info.flags & NN_PRISMTECH_FL_PARTICIPANT_IS_DDSI2))
      custom_flags |= CF_PARTICIPANT_IS_DDSI2;

    TRACE ((" (0x%08x-0x%08x-0x%08x-0x%08x-0x%08x %s)",
            datap->prismtech_participant_version_info.version,
            datap->prismtech_participant_version_info.flags,
            datap->prismtech_participant_version_info.unused[0],
            datap->prismtech_participant_version_info.unused[1],
            datap->prismtech_participant_version_info.unused[2],
            datap->prismtech_participant_version_info.internals));
  }

  /* Choose locators */
  {
    nn_locator_t loc;
    int uc_same_subnet;

    as_default = new_addrset ();
    as_meta = new_addrset ();

    if ((datap->present & PP_DEFAULT_MULTICAST_LOCATOR) && (get_locator (&loc, &datap->default_multicast_locators, 0)))
      allowmulticast_aware_add_to_addrset (as_default, &loc);
    if ((datap->present & PP_METATRAFFIC_MULTICAST_LOCATOR) && (get_locator (&loc, &datap->metatraffic_multicast_locators, 0)))
      allowmulticast_aware_add_to_addrset (as_meta, &loc);

    /* If no multicast locators or multicast TTL > 1, assume IP (multicast) routing can be relied upon to reach
       the remote participant, else only accept nodes with an advertised unicast address in the same subnet to
       protect against multicasts being received over an unexpected interface (which sometimes appears to occur) */
    if (addrset_empty_mc (as_default) && addrset_empty_mc (as_meta))
      uc_same_subnet = 0;
    else if (config.multicast_ttl > 1)
      uc_same_subnet = 0;
    else
    {
      uc_same_subnet = 1;
      TRACE ((" subnet-filter"));
    }

    /* If unicast locators not present, then try to obtain from connection */
    if ((datap->present & PP_DEFAULT_UNICAST_LOCATOR) && (get_locator (&loc, &datap->default_unicast_locators, uc_same_subnet)))
      add_to_addrset (as_default, &loc);
    else if (ddsi_conn_peer_locator (rst->conn, &loc))
      add_to_addrset (as_default, &loc);

    if ((datap->present & PP_METATRAFFIC_UNICAST_LOCATOR) && (get_locator (&loc, &datap->metatraffic_unicast_locators, uc_same_subnet)))
      add_to_addrset (as_meta, &loc);
    else if (ddsi_conn_peer_locator (rst->conn, &loc))
      add_to_addrset (as_meta, &loc);

    nn_log_addrset (LC_TRACE, " (data", as_default);
    nn_log_addrset (LC_TRACE, " meta", as_meta);
    TRACE ((")"));
  }

  if (addrset_empty_uc (as_default) || addrset_empty_uc (as_meta))
  {
    TRACE ((" (no unicast address)"));
    unref_addrset (as_default);
    unref_addrset (as_meta);
    return;
  }

  TRACE ((" QOS={"));
  nn_log_xqos (LC_TRACE, &datap->qos);
  TRACE (("}\n"));

  maybe_add_pp_as_meta_to_as_disc (as_meta);

  new_proxy_participant
  (
    &datap->participant_guid,
    builtin_endpoint_set,
    prismtech_builtin_endpoint_set,
    &privileged_pp_guid,
    as_default,
    as_meta,
    datap,
    nn_from_ddsi_duration (lease_duration),
    rst->vendor,
    custom_flags
  );

  /* Force transmission of SPDP messages - we're not very careful
     in avoiding the processing of SPDP packets addressed to others
     so filter here */
  {
    int have_dst =
      (rst->dst_guid_prefix.u[0] != 0 || rst->dst_guid_prefix.u[1] != 0 || rst->dst_guid_prefix.u[2] != 0);
    if (!have_dst)
    {
      TRACE (("broadcasted SPDP packet -> answering"));
      respond_to_spdp (&datap->participant_guid);
    }
    else
    {
      TRACE (("directed SPDP packet -> not responding"));
    }
  }
}

static void handle_SPDP (const struct receiver_state *rst, unsigned statusinfo, const void *vdata, unsigned len)
{
  const struct CDRHeader *data = vdata; /* built-ins not deserialized (yet) */
  TRACE (("SPDP ST%x", statusinfo));
  if (data == NULL)
  {
    TRACE ((" no payload?\n"));
    return;
  }
  else
  {
    nn_plist_t decoded_data;
    nn_plist_src_t src;
    src.protocol_version = rst->protocol_version;
    src.vendorid = rst->vendor;
    src.encoding = data->identifier;
    src.buf = (unsigned char *) data + 4;
    src.bufsz = len - 4;
    if (nn_plist_init_frommsg (&decoded_data, NULL, ~(uint64_t)0, ~(uint64_t)0, &src) < 0)
    {
      NN_WARNING2 ("SPDP (vendor %d.%d): invalid qos/parameters\n", src.vendorid.id[0], src.vendorid.id[1]);
      return;
    }

    switch (statusinfo & (NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER))
    {
      case 0:
        handle_SPDP_alive (rst, &decoded_data);
        break;

      case NN_STATUSINFO_DISPOSE:
      case NN_STATUSINFO_UNREGISTER:
      case (NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER):
        handle_SPDP_dead (rst, &decoded_data);
        break;
    }

    nn_plist_fini (&decoded_data);
    TRACE (("\n"));
  }
}

static void add_sockaddr_to_ps (const nn_locator_t *loc, void *arg)
{
  nn_plist_t *ps = (nn_plist_t *) arg;
  struct nn_locators_one *elem = os_malloc (sizeof (struct nn_locators_one));
  struct nn_locators *locs;
  unsigned present_flag;

  elem->loc = *loc;
  elem->next = NULL;

  if (is_mcaddr (loc)) {
    locs = &ps->multicast_locators;
    present_flag = PP_MULTICAST_LOCATOR;
  } else {
    locs = &ps->unicast_locators;
    present_flag = PP_UNICAST_LOCATOR;
  }

  if (!(ps->present & present_flag))
  {
    locs->n = 0;
    locs->first = locs->last = NULL;
    ps->present |= present_flag;
  }
  locs->n++;
  if (locs->first)
    locs->last->next = elem;
  else
    locs->first = elem;
  locs->last = elem;
}

/******************************************************************************
 ***
 *** SEDP
 ***
 *****************************************************************************/

static int sedp_write_endpoint
(
   struct writer *wr, int end_of_life, const nn_guid_t *epguid,
   const struct entity_common *common, const struct endpoint_common *epcommon,
   const nn_xqos_t *xqos, struct addrset *as)
{
  const nn_xqos_t *defqos = is_writer_entityid (epguid->entityid) ? &gv.default_xqos_wr : &gv.default_xqos_rd;
  const nn_vendorid_t my_vendor_id = MY_VENDOR_ID;
  const int just_key = end_of_life;
  struct nn_xmsg *mpayload;
  uint64_t qosdiff;
  nn_guid_t kh;
  nn_plist_t ps;
  serstate_t serstate;
  serdata_t serdata;
  void *payload_blob;
  size_t payload_sz;
  unsigned statusinfo;

  nn_plist_init_empty (&ps);
  ps.present |= PP_ENDPOINT_GUID;
  ps.endpoint_guid = *epguid;
#if ! LITE
  if (epcommon && !gid_is_fake (&epcommon->gid))
  {
    /* Built-in endpoints are not published, so the absence of a GID for
     those doesn't matter, but, e.g., the fictitious transient data readers
     also don't have a GID.  By not publishing a GID, we ensure the other
     side will fake it. */
    ps.present |= PP_PRISMTECH_ENDPOINT_GID;
    ps.endpoint_gid = epcommon->gid;
  }
#endif

  if (common && *common->name != 0)
  {
    ps.present |= PP_ENTITY_NAME;
    ps.entity_name = common->name;
  }

  if (end_of_life)
  {
    assert (xqos == NULL);
    assert (epcommon == NULL);
    qosdiff = 0;
  }
  else
  {
    assert (xqos != NULL);
    assert (epcommon != NULL);
    ps.present |= PP_PROTOCOL_VERSION | PP_VENDORID;
    ps.protocol_version.major = RTPS_MAJOR;
    ps.protocol_version.minor = RTPS_MINOR;
    ps.vendorid = my_vendor_id;

    if (epcommon->group_guid.entityid.u != 0)
    {
      ps.present |= PP_GROUP_GUID;
      ps.group_guid = epcommon->group_guid;
#if ! LITE
      ps.present |= PP_PRISMTECH_GROUP_GID;
      ps.group_gid = epcommon->group_gid;
#endif
    }

#ifdef DDSI_INCLUDE_SSM
    /* A bit of a hack -- the easy alternative would be to make it yet
     another parameter.  We only set "reader favours SSM" if we
     really do: no point in telling the world that everything is at
     the default. */
    if (!is_writer_entityid (epguid->entityid))
    {
      const struct reader *rd = ephash_lookup_reader_guid (epguid);
      assert (rd);
      if (rd->favours_ssm)
      {
        ps.present |= PP_READER_FAVOURS_SSM;
        ps.reader_favours_ssm.state = 1u;
      }
    }
#endif

    qosdiff = nn_xqos_delta (xqos, defqos, ~(uint64_t)0);
    if (config.explicitly_publish_qos_set_to_default)
      qosdiff |= ~QP_UNRECOGNIZED_INCOMPATIBLE_MASK;

    if (as)
    {
      addrset_forall (as, add_sockaddr_to_ps, &ps);
    }
  }

  /* The message is only a temporary thing, used only for encoding
     the QoS and other settings. So the header fields aren't really
     important, except that they need to be set to reasonable things
     or it'll crash */
  mpayload = nn_xmsg_new (gv.xmsgpool, &wr->e.guid.prefix, 0, NN_XMSG_KIND_DATA);
  nn_plist_addtomsg (mpayload, &ps, ~(uint64_t)0, ~(uint64_t)0);
  if (xqos) nn_xqos_addtomsg (mpayload, xqos, qosdiff);
  nn_xmsg_addpar_sentinel (mpayload);

  /* Then we take the payload from the message and turn it into a
     serdata, and then we can write it as normal data */
  serstate = ddsi_serstate_new (gv.serpool, NULL);
  payload_blob = nn_xmsg_payload (&payload_sz, mpayload);
  ddsi_serstate_append_blob (serstate, 4, payload_sz, payload_blob);
  kh = nn_hton_guid (*epguid);
  serstate_set_key (serstate, just_key, &kh);
  if (end_of_life)
    statusinfo = NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER;
  else
    statusinfo = 0;
  ddsi_serstate_set_msginfo (serstate, statusinfo, now (), NULL);
  serdata = ddsi_serstate_fix (serstate);
  nn_xmsg_free (mpayload);

  TRACE (("sedp: write for %x:%x:%x:%x via %x:%x:%x:%x\n", PGUID (*epguid), PGUID (wr->e.guid)));
  return write_sample (NULL, wr, serdata);
}

static struct writer *get_sedp_writer (const struct participant *pp, unsigned entityid)
{
  struct writer *sedp_wr = get_builtin_writer (pp, entityid);
  if (sedp_wr == NULL)
    NN_FATAL2 ("sedp_write_writer: no SEDP builtin writer %x for %x:%x:%x:%x\n", entityid, PGUID (pp->e.guid));
  return sedp_wr;
}

int sedp_write_writer (struct writer *wr)
{
  if (! is_builtin_entityid (wr->e.guid.entityid, ownvendorid))
  {
    struct writer *sedp_wr = get_sedp_writer (wr->c.pp, NN_ENTITYID_SEDP_BUILTIN_PUBLICATIONS_WRITER);
#ifdef DDSI_INCLUDE_SSM
    struct addrset *as = wr->ssm_as;
#else
    struct addrset *as = NULL;
#endif
    return sedp_write_endpoint (sedp_wr, 0, &wr->e.guid, &wr->e, &wr->c, wr->xqos, as);
  }
  return 0;
}

int sedp_write_reader (struct reader *rd)
{
  if (! is_builtin_entityid (rd->e.guid.entityid, ownvendorid))
  {
    struct writer *sedp_wr = get_sedp_writer (rd->c.pp, NN_ENTITYID_SEDP_BUILTIN_SUBSCRIPTIONS_WRITER);
#ifdef DDSI_INCLUDE_NETWORK_PARTITIONS
    struct addrset *as = rd->as;
#else
    struct addrset *as = NULL;
#endif
    return sedp_write_endpoint (sedp_wr, 0, &rd->e.guid, &rd->e, &rd->c, rd->xqos, as);
  }
  return 0;
}

int sedp_dispose_unregister_writer (struct writer *wr)
{
  if (! is_builtin_entityid (wr->e.guid.entityid, ownvendorid))
  {
    struct writer *sedp_wr = get_sedp_writer (wr->c.pp, NN_ENTITYID_SEDP_BUILTIN_PUBLICATIONS_WRITER);
    return sedp_write_endpoint (sedp_wr, 1, &wr->e.guid, NULL, NULL, NULL, NULL);
  }
  return 0;
}

int sedp_dispose_unregister_reader (struct reader *rd)
{
  if (! is_builtin_entityid (rd->e.guid.entityid, ownvendorid))
  {
    struct writer *sedp_wr = get_sedp_writer (rd->c.pp, NN_ENTITYID_SEDP_BUILTIN_SUBSCRIPTIONS_WRITER);
    return sedp_write_endpoint (sedp_wr, 1, &rd->e.guid, NULL, NULL, NULL, NULL);
  }
  return 0;
}

static const char *durability_to_string (nn_durability_kind_t k)
{
  switch (k)
  {
    case NN_VOLATILE_DURABILITY_QOS: return "volatile";
    case NN_TRANSIENT_LOCAL_DURABILITY_QOS: return "transient-local";
    case NN_TRANSIENT_DURABILITY_QOS: return "transient";
    case NN_PERSISTENT_DURABILITY_QOS: return "persistent";
  }
  abort (); return 0;
}

static void handle_SEDP_alive (nn_plist_t *datap /* note: potentially modifies datap */, const nn_guid_prefix_t *src_guid_prefix, nn_vendorid_t vendorid)
{
#define E(msg, lbl) do { nn_log (LC_TRACE, (msg)); goto lbl; } while (0)
  struct proxy_participant *pp;
  struct proxy_writer * pwr = NULL;
  struct proxy_reader * prd = NULL;
  nn_guid_t ppguid;
  nn_xqos_t *xqos;
  int reliable;
  struct addrset *as;
  int is_writer;
#ifdef DDSI_INCLUDE_SSM
  int ssm;
#endif

  assert (datap);

  if (!(datap->present & PP_ENDPOINT_GUID))
    E (" no guid?\n", err);
  TRACE ((" %x:%x:%x:%x", PGUID (datap->endpoint_guid)));

  ppguid.prefix = datap->endpoint_guid.prefix;
  ppguid.entityid.u = NN_ENTITYID_PARTICIPANT;
  if (is_deleted_participant_guid (&ppguid, DPG_REMOTE))
    E (" local dead pp?\n", err);

  if
  (
#if LITE
    (! config.enableLoopback) &&
#endif
    (ephash_lookup_participant_guid (&ppguid) != NULL)
  )
    E (" local pp?\n", err);

  if (is_builtin_entityid (datap->endpoint_guid.entityid, vendorid))
    E (" built-in\n", err);
  if (!(datap->qos.present & QP_TOPIC_NAME))
    E (" no topic?\n", err);
  if (!(datap->qos.present & QP_TYPE_NAME))
    E (" no typename?\n", err);

  if ((pp = ephash_lookup_proxy_participant_guid (&ppguid)) == NULL)
  {
    TRACE ((" unknown-proxypp"));
    if (memcmp (&ppguid.prefix, src_guid_prefix, sizeof (ppguid.prefix)) != 0 && vendor_is_cloud (vendorid)) {
      nn_plist_t pp_plist;
      nn_guid_t ds_pp_guid;
      /* Some endpoint that we discovered through the DS, but then it must have at least some locators */
      ds_pp_guid.prefix = *src_guid_prefix;
      ds_pp_guid.entityid = to_entityid (NN_ENTITYID_PARTICIPANT);
      TRACE ((" from-DS %x:%x:%x:%x", PGUID (ds_pp_guid)));
      /* avoid "no address" case, so we never create the proxy participant for nothing (FIXME: rework some of this) */
      if (!(datap->present & (PP_UNICAST_LOCATOR | PP_MULTICAST_LOCATOR)))
        E (" data locator absent\n", err);
      TRACE ((" new-proxypp %x:%x:%x:%x\n", PGUID (ppguid)));
      nn_plist_init_empty(&pp_plist);
      new_proxy_participant(&ppguid, 0, 0, &ds_pp_guid, new_addrset(), new_addrset(), &pp_plist, T_NEVER, vendorid, CF_IMPLICITLY_CREATED_PROXYPP);
      pp = ephash_lookup_proxy_participant_guid (&ppguid);
      /* Repeat regular SEDP trace for convenience */
      TRACE (("SEDP ST0 %x:%x:%x:%x (cont)", PGUID (datap->endpoint_guid)));
      assert(pp != NULL);
    } else {
      E ("?\n", err);
    }
  }

  xqos = &datap->qos;
  is_writer = is_writer_entityid (datap->endpoint_guid.entityid);
  if (!is_writer)
    nn_xqos_mergein_missing (xqos, &gv.default_xqos_rd);
  else if (vendor_is_prismtech(vendorid))
    nn_xqos_mergein_missing (xqos, &gv.default_xqos_wr);
  else
    nn_xqos_mergein_missing (xqos, &gv.default_xqos_wr_nad);

  /* After copy + merge, should have at least the ones present in the
     input.  Also verify reliability and durability are present,
     because we explicitly read those. */
  assert ((xqos->present & datap->qos.present) == datap->qos.present);
  assert (xqos->present & QP_RELIABILITY);
  assert (xqos->present & QP_DURABILITY);
  reliable = (xqos->reliability.kind == NN_RELIABLE_RELIABILITY_QOS);

  nn_log (LC_TRACE, " %s %s %s: %s%s.%s/%s",
          reliable ? "reliable" : "best-effort",
          durability_to_string (xqos->durability.kind),
          is_writer ? "writer" : "reader",
          ((!(xqos->present & QP_PARTITION) || xqos->partition.n == 0 || *xqos->partition.strs[0] == '\0')
           ? "(default)" : xqos->partition.strs[0]),
          ((xqos->present & QP_PARTITION) && xqos->partition.n > 1) ? "+" : "",
          xqos->topic_name, xqos->type_name);

  if (! is_writer && (datap->present & PP_EXPECTS_INLINE_QOS) && datap->expects_inline_qos)
  {
    E ("******* AARGH - it expects inline QoS ********\n", err);
  }

  if (is_writer)
  {
    pwr = ephash_lookup_proxy_writer_guid (&datap->endpoint_guid);
  }
  else
  {
    prd = ephash_lookup_proxy_reader_guid (&datap->endpoint_guid);
  }
  if (pwr || prd)
  {
    /* Cloud load balances by updating participant endpoints */

    if (! vendor_is_cloud (vendorid))
    {
      TRACE ((" known\n"));
      goto err;
    }
    TRACE ((" known-DS\n"));
  }
  else
  {
    TRACE ((" NEW"));
  }

  {
    nn_locator_t loc;
    as = new_addrset ();
    if ((datap->present & PP_UNICAST_LOCATOR) && get_locator (&loc, &datap->unicast_locators, 0))
      add_to_addrset (as, &loc);
    else
      copy_addrset_into_addrset_uc (as, pp->as_default);
    if ((datap->present & PP_MULTICAST_LOCATOR) && get_locator (&loc, &datap->multicast_locators, 0))
      allowmulticast_aware_add_to_addrset (as, &loc);
    else
      copy_addrset_into_addrset_mc (as, pp->as_default);
  }
  if (addrset_empty (as))
  {
    unref_addrset (as);
    E (" no address", err);
  }

  nn_log_addrset (LC_TRACE, " (as", as);
#ifdef DDSI_INCLUDE_SSM
  ssm = 0;
  if (is_writer)
    ssm = addrset_contains_ssm (as);
  else if (datap->present & PP_READER_FAVOURS_SSM)
    ssm = (datap->reader_favours_ssm.state != 0);
  TRACE ((" ssm=%u", ssm));
#endif
  TRACE ((") QOS={"));
  nn_log_xqos (LC_TRACE, xqos);
  TRACE (("}\n"));

  if ((datap->endpoint_guid.entityid.u & NN_ENTITYID_SOURCE_MASK) == NN_ENTITYID_SOURCE_VENDOR && !vendor_is_prismtech (vendorid))
  {
    TRACE (("ignoring vendor-specific endpoint %x:%x:%x:%x\n", PGUID (datap->endpoint_guid)));
  }
  else
  {
#if ! LITE
    if ((datap->present & PP_GROUP_GUID) && vendor_is_prismtech (vendorid))
    {
      /* For PrismTech peers, make sure we have a proxy group: not
         because we will make it public or rely on it in any way - yet -
         in the reader/writer handling, but because for Cafe we need to
         map the GUIDs to GIDs ... */
      if (memcmp (&datap->group_guid.prefix, &ppguid.prefix, sizeof (datap->group_guid.prefix)) == 0)
      {
         if (datap->present & PP_PRISMTECH_GROUP_GID)
         {
            struct v_gid_s *gid = (datap->present & PP_PRISMTECH_GROUP_GID) ? &datap->group_gid : NULL;
            new_proxy_group (&datap->group_guid, gid, NULL, NULL);
         }
      }
    }
#endif
    if (is_writer)
    {
      if (pwr)
      {
        update_proxy_writer (pwr, as);
      }
      else
      {
        /* not supposed to get here for built-in ones, so can determine the channel based on the transport priority */
        assert (!is_builtin_entityid (datap->endpoint_guid.entityid, vendorid));
#ifdef DDSI_INCLUDE_NETWORK_CHANNELS
        {
          struct config_channel_listelem *channel = find_channel (xqos->transport_priority);
          new_proxy_writer (&ppguid, &datap->endpoint_guid, as, datap, channel->dqueue, channel->evq ? channel->evq : gv.xevents);
        }
#else
        new_proxy_writer (&ppguid, &datap->endpoint_guid, as, datap, gv.user_dqueue, gv.xevents);
#endif
      }
    }
    else
    {
      if (prd)
      {
        update_proxy_reader (prd, as);
      }
      else
      {
#ifdef DDSI_INCLUDE_SSM
        new_proxy_reader (&ppguid, &datap->endpoint_guid, as, datap, ssm);
#else
        new_proxy_reader (&ppguid, &datap->endpoint_guid, as, datap);
#endif
      }
    }
  }

  unref_addrset (as);

err:

  return;
#undef E
}

static void handle_SEDP_dead (nn_plist_t *datap)
{
  int res;
  if (!(datap->present & PP_ENDPOINT_GUID))
  {
    TRACE ((" no guid?\n"));
    return;
  }
  TRACE ((" %x:%x:%x:%x", PGUID (datap->endpoint_guid)));
  if (is_writer_entityid (datap->endpoint_guid.entityid))
  {
    res = delete_proxy_writer (&datap->endpoint_guid, 0);
  }
  else
  {
    res = delete_proxy_reader (&datap->endpoint_guid, 0);
  }
  TRACE ((" %s\n", (res < 0) ? " unknown" : " delete"));
}

static void handle_SEDP (const struct receiver_state *rst, unsigned statusinfo, const void *vdata, unsigned len)
{
  const struct CDRHeader *data = vdata; /* built-ins not deserialized (yet) */
  TRACE (("SEDP ST%x", statusinfo));
  if (data == NULL)
  {
    TRACE ((" no payload?\n"));
    return;
  }
  else
  {
    nn_plist_t decoded_data;
    nn_plist_src_t src;
    src.protocol_version = rst->protocol_version;
    src.vendorid = rst->vendor;
    src.encoding = data->identifier;
    src.buf = (unsigned char *) data + 4;
    src.bufsz = len - 4;
    if (nn_plist_init_frommsg (&decoded_data, NULL, ~(uint64_t)0, ~(uint64_t)0, &src) < 0)
    {
      NN_WARNING2 ("SEDP (vendor %d.%d): invalid qos/parameters\n", src.vendorid.id[0], src.vendorid.id[1]);
      return;
    }

    switch (statusinfo & (NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER))
    {
      case 0:
        handle_SEDP_alive (&decoded_data, &rst->src_guid_prefix, rst->vendor);
        break;

      case NN_STATUSINFO_DISPOSE:
      case NN_STATUSINFO_UNREGISTER:
      case (NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER):
        handle_SEDP_dead (&decoded_data);
        break;
    }

    nn_plist_fini (&decoded_data);
  }
}

/******************************************************************************
 ***
 *** Topics
 ***
 *****************************************************************************/

int sedp_write_topic (struct participant *pp, const struct nn_plist *datap)
{
  struct writer *sedp_wr;
  struct nn_xmsg *mpayload;
  serstate_t serstate;
  serdata_t serdata;
  void *payload_blob;
  size_t payload_sz;
  uint32_t topic_name_sz;
  uint32_t topic_name_sz_BE;
  uint64_t delta;
  unsigned char digest[16];
  md5_state_t md5st;

  assert (datap->qos.present & QP_TOPIC_NAME);

  sedp_wr = get_sedp_writer (pp, NN_ENTITYID_SEDP_BUILTIN_TOPIC_WRITER);

  mpayload = nn_xmsg_new (gv.xmsgpool, &sedp_wr->e.guid.prefix, 0, NN_XMSG_KIND_DATA);
  delta = nn_xqos_delta (&datap->qos, &gv.default_xqos_tp, ~(uint64_t)0);
  if (config.explicitly_publish_qos_set_to_default)
    delta |= ~QP_UNRECOGNIZED_INCOMPATIBLE_MASK;
  nn_plist_addtomsg (mpayload, datap, ~(uint64_t)0, delta);
  nn_xmsg_addpar_sentinel (mpayload);

  serstate = ddsi_serstate_new (gv.serpool, NULL);
  payload_blob = nn_xmsg_payload (&payload_sz, mpayload);
  ddsi_serstate_append_blob (serstate, 4, payload_sz, payload_blob);

  topic_name_sz = (uint32_t) strlen (datap->qos.topic_name) + 1;
  topic_name_sz_BE = toBE4u (topic_name_sz);

  md5_init (&md5st);
  md5_append (&md5st, (const md5_byte_t *) &topic_name_sz_BE, sizeof (topic_name_sz_BE));
  md5_append (&md5st, (const md5_byte_t *) datap->qos.topic_name, topic_name_sz);
  md5_finish (&md5st, digest);

  serstate_set_key (serstate, 0, digest);
  ddsi_serstate_set_msginfo (serstate, 0, now (), NULL);
  serdata = ddsi_serstate_fix (serstate);
  nn_xmsg_free (mpayload);

  TRACE (("sedp: write topic %s via %x:%x:%x:%x\n", datap->qos.topic_name, PGUID (sedp_wr->e.guid)));
  return write_sample (NULL, sedp_wr, serdata);
}

#if ! LITE
static void handle_SEDP_TOPIC (const struct receiver_state *rst, nn_entityid_t wr_entity_id, unsigned statusinfo, const void *vdata, unsigned len)
{
  const struct CDRHeader *data = vdata; /* built-ins not deserialized (yet) */
  TRACE (("SEDP_TOPIC ST%x", statusinfo));
  assert (wr_entity_id.u == NN_ENTITYID_SEDP_BUILTIN_TOPIC_WRITER);
  (void) wr_entity_id;
  if (data == NULL)
  {
    TRACE ((" no payload?\n"));
    return;
  }
  else
  {
    nn_plist_t decoded_data;
    nn_plist_src_t src;
    src.protocol_version = rst->protocol_version;
    src.vendorid = rst->vendor;
    src.encoding = data->identifier;
    src.buf = (unsigned char *) data + 4;
    src.bufsz = len - 4;
    if (nn_plist_init_frommsg (&decoded_data, NULL, ~(uint64_t)0, ~(uint64_t)0, &src) < 0)
    {
      NN_WARNING2 ("SEDP_TOPIC (vendor %d.%d): invalid qos/parameters\n", src.vendorid.id[0], src.vendorid.id[1]);
      return;
    }

    /* Is there any point in maintaining proxy topics? Within the context of OSPL, topics come but never go, and all topics are consistent - and so we can simply publish the DCPSTopic built-in topic when we receive a definition. Within the context of DDS, topics and come and go; and within that of DDSI, topics are not necessarily consistent. So for a full implementation, we will probably need to maintain proxy topics per participant ... But for simply supporting built-in topics in OSPL, we don't. */
    if ((statusinfo & (NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER)) == 0)
    {
      if (!((decoded_data.present & PP_PRISMTECH_TYPE_DESCRIPTION) &&
            (decoded_data.qos.present & QP_TOPIC_NAME) &&
            (decoded_data.qos.present & QP_TYPE_NAME)))
      {
        const char *name = (decoded_data.qos.present & QP_TOPIC_NAME) ? decoded_data.qos.topic_name : "(anonymous)";
        nn_log (LC_DISCOVERY, "SEDP_TOPIC (vendor %d.%d): missing qos/parameters %"PA_PRIx64"/%"PA_PRIx64" for topic %s\n", src.vendorid.id[0], src.vendorid.id[1], decoded_data.present, decoded_data.qos.present, name);
      }
      else
      {
        TRACE ((" %s/%s QOS={", decoded_data.qos.topic_name, decoded_data.qos.type_name));
        nn_xqos_mergein_missing (&decoded_data.qos, &gv.default_xqos_tp);
        nn_log_xqos (LC_TRACE, &decoded_data.qos);
        TRACE (("}"));
        write_builtin_topic_proxy_topic (&decoded_data);
      }
    }

    nn_plist_fini (&decoded_data);
  }
  TRACE (("\n"));
}
#endif

/******************************************************************************
 ***
 *** PrismTech CM data
 ***
 *****************************************************************************/

int sedp_write_cm_participant (struct participant *pp, int alive)
{
  struct writer * const sedp_wr = get_sedp_writer (pp, NN_ENTITYID_SEDP_BUILTIN_CM_PARTICIPANT_WRITER);
  struct nn_xmsg *mpayload;
  serstate_t serstate;
  serdata_t serdata;
  nn_plist_t ps;
  nn_guid_t kh;
  void *payload_blob;
  size_t payload_sz;
  unsigned statusinfo;

  /* The message is only a temporary thing, used only for encoding
   the QoS and other settings. So the header fields aren't really
   important, except that they need to be set to reasonable things
   or it'll crash */
  mpayload = nn_xmsg_new (gv.xmsgpool, &sedp_wr->e.guid.prefix, 0, NN_XMSG_KIND_DATA);
  nn_plist_init_empty (&ps);
  ps.present = PP_PARTICIPANT_GUID;
  ps.participant_guid = pp->e.guid;
  if (alive)
  {
    nn_plist_addtomsg (mpayload, &ps, ~(uint64_t)0, ~(uint64_t)0);
    nn_plist_addtomsg (mpayload, pp->plist,
                       PP_PRISMTECH_NODE_NAME | PP_PRISMTECH_EXEC_NAME | PP_PRISMTECH_PROCESS_ID |
                       PP_PRISMTECH_WATCHDOG_SCHEDULING | PP_PRISMTECH_LISTENER_SCHEDULING |
                       PP_PRISMTECH_SERVICE_TYPE | PP_ENTITY_NAME,
                       QP_PRISMTECH_ENTITY_FACTORY);
  }
  nn_xmsg_addpar_sentinel (mpayload);

  /* Then we take the payload from the message and turn it into a
   serdata, and then we can write it as normal data */
  serstate = ddsi_serstate_new (gv.serpool, NULL);
  payload_blob = nn_xmsg_payload (&payload_sz, mpayload);
  ddsi_serstate_append_blob (serstate, 4, payload_sz, payload_blob);
  kh = nn_hton_guid (pp->e.guid);
  serstate_set_key (serstate, !alive, &kh);
  if (!alive)
    statusinfo = NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER;
  else
    statusinfo = 0;
  ddsi_serstate_set_msginfo (serstate, statusinfo, now (), NULL);
  serdata = ddsi_serstate_fix (serstate);
  nn_xmsg_free (mpayload);

  TRACE (("sedp: write CMParticipant ST%x for %x:%x:%x:%x via %x:%x:%x:%x\n", statusinfo, PGUID (pp->e.guid), PGUID (sedp_wr->e.guid)));
  return write_sample (NULL, sedp_wr, serdata);
}

static void handle_SEDP_CM (const struct receiver_state *rst, nn_entityid_t wr_entity_id, unsigned statusinfo, const void *vdata, unsigned len)
{
  const struct CDRHeader *data = vdata; /* built-ins not deserialized (yet) */
  TRACE (("SEDP_CM ST%x", statusinfo));
  assert (wr_entity_id.u == NN_ENTITYID_SEDP_BUILTIN_CM_PARTICIPANT_WRITER);
  (void) wr_entity_id;
  if (data == NULL)
  {
    TRACE ((" no payload?\n"));
    return;
  }
  else
  {
    nn_plist_t decoded_data;
    nn_plist_src_t src;
    src.protocol_version = rst->protocol_version;
    src.vendorid = rst->vendor;
    src.encoding = data->identifier;
    src.buf = (unsigned char *) data + 4;
    src.bufsz = len - 4;
    if (nn_plist_init_frommsg (&decoded_data, NULL, ~(uint64_t)0, ~(uint64_t)0, &src) < 0)
    {
      NN_WARNING2 ("SEDP_CM (vendor %d.%d): invalid qos/parameters\n", src.vendorid.id[0], src.vendorid.id[1]);
      return;
    }

    /* ignore: dispose/unregister is tied to deleting the participant, which will take care of the dispose/unregister for us */;
    if ((statusinfo & (NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER)) == 0)
    {
      struct proxy_participant *proxypp;
      if (!(decoded_data.present & PP_PARTICIPANT_GUID))
        NN_WARNING2 ("SEDP_CM (vendor %d.%d): missing participant GUID\n", src.vendorid.id[0], src.vendorid.id[1]);
      else if ((proxypp = ephash_lookup_proxy_participant_guid (&decoded_data.participant_guid)) != NULL)
        update_proxy_participant_plist (proxypp, &decoded_data);
    }

    nn_plist_fini (&decoded_data);
  }
  TRACE (("\n"));
}

static struct participant *group_guid_to_participant (const nn_guid_t *group_guid)
{
  nn_guid_t ppguid;
  ppguid.prefix = group_guid->prefix;
  ppguid.entityid.u = NN_ENTITYID_PARTICIPANT;
  return ephash_lookup_participant_guid (&ppguid);
}

int sedp_write_cm_publisher (const struct nn_plist *datap, int alive)
{
  struct participant *pp;
  struct writer *sedp_wr;
  struct nn_xmsg *mpayload;
  serstate_t serstate;
  serdata_t serdata;
  nn_guid_t kh;
  void *payload_blob;
  size_t payload_sz;
  unsigned statusinfo;
  uint64_t delta;

  if ((pp = group_guid_to_participant (&datap->group_guid)) == NULL)
  {
      TRACE (("sedp: write CMPublisher alive:%d for %x:%x:%x:%x dropped: no participant\n",
              alive, PGUID (datap->group_guid)));
      return 0;
  }
  sedp_wr = get_sedp_writer (pp, NN_ENTITYID_SEDP_BUILTIN_CM_PUBLISHER_WRITER);

  /* The message is only a temporary thing, used only for encoding
   the QoS and other settings. So the header fields aren't really
   important, except that they need to be set to reasonable things
   or it'll crash */
  mpayload = nn_xmsg_new (gv.xmsgpool, &sedp_wr->e.guid.prefix, 0, NN_XMSG_KIND_DATA);
  if (!alive)
    delta = 0;
  else
  {
    delta = nn_xqos_delta (&datap->qos, &gv.default_xqos_pub, ~(uint64_t)0);
    if (!config.explicitly_publish_qos_set_to_default)
      delta |= ~QP_UNRECOGNIZED_INCOMPATIBLE_MASK;
  }
  nn_plist_addtomsg (mpayload, datap, ~(uint64_t)0, delta);
  nn_xmsg_addpar_sentinel (mpayload);

  /* Then we take the payload from the message and turn it into a
   serdata, and then we can write it as normal data */
  serstate = ddsi_serstate_new (gv.serpool, NULL);
  payload_blob = nn_xmsg_payload (&payload_sz, mpayload);
  ddsi_serstate_append_blob (serstate, 4, payload_sz, payload_blob);
  kh = nn_hton_guid (datap->group_guid);
  serstate_set_key (serstate, !alive, &kh);
  if (!alive)
    statusinfo = NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER;
  else
    statusinfo = 0;
  ddsi_serstate_set_msginfo (serstate, statusinfo, now (), NULL);
  serdata = ddsi_serstate_fix (serstate);
  nn_xmsg_free (mpayload);

#if ! LITE
  TRACE (("sedp: write CMPublisher ST%x for %x:%x:%x:%x (%x:%x:%x) via %x:%x:%x:%x\n",
          statusinfo, PGUID (datap->group_guid),
          datap->group_gid.systemId, datap->group_gid.localId, datap->group_gid.serial,
          PGUID (sedp_wr->e.guid)));
#endif
  return write_sample (NULL, sedp_wr, serdata);
}

int sedp_write_cm_subscriber (const struct nn_plist *datap, int alive)
{
  struct participant *pp;
  struct writer *sedp_wr;
  struct nn_xmsg *mpayload;
  serstate_t serstate;
  serdata_t serdata;
  nn_guid_t kh;
  void *payload_blob;
  size_t payload_sz;
  unsigned statusinfo;
  uint64_t delta;

  if ((pp = group_guid_to_participant (&datap->group_guid)) == NULL)
  {
      TRACE (("sedp: write CMSubscriber alive:%d for %x:%x:%x:%x dropped: no participant\n",
              alive, PGUID (datap->group_guid)));
      return 0;
  }
  sedp_wr = get_sedp_writer (pp, NN_ENTITYID_SEDP_BUILTIN_CM_SUBSCRIBER_WRITER);

  /* The message is only a temporary thing, used only for encoding
   the QoS and other settings. So the header fields aren't really
   important, except that they need to be set to reasonable things
   or it'll crash */
  mpayload = nn_xmsg_new (gv.xmsgpool, &sedp_wr->e.guid.prefix, 0, NN_XMSG_KIND_DATA);
  if (!alive)
    delta = 0;
  else
  {
    delta = nn_xqos_delta (&datap->qos, &gv.default_xqos_sub, ~(uint64_t)0);
    if (!config.explicitly_publish_qos_set_to_default)
      delta |= ~QP_UNRECOGNIZED_INCOMPATIBLE_MASK;
  }
  nn_plist_addtomsg (mpayload, datap, ~(uint64_t)0, delta);
  nn_xmsg_addpar_sentinel (mpayload);

  /* Then we take the payload from the message and turn it into a
   serdata, and then we can write it as normal data */
  serstate = ddsi_serstate_new (gv.serpool, NULL);
  payload_blob = nn_xmsg_payload (&payload_sz, mpayload);
  ddsi_serstate_append_blob (serstate, 4, payload_sz, payload_blob);
  kh = nn_hton_guid (datap->group_guid);
  serstate_set_key (serstate, !alive, &kh);
  if (!alive)
    statusinfo = NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER;
  else
    statusinfo = 0;
  ddsi_serstate_set_msginfo (serstate, statusinfo, now (), NULL);
  serdata = ddsi_serstate_fix (serstate);
  nn_xmsg_free (mpayload);

#if ! LITE
  TRACE (("sedp: write CMSubscriber ST%x for %x:%x:%x:%x (%x:%x:%x) via %x:%x:%x:%x\n",
          statusinfo, PGUID (datap->group_guid),
          datap->group_gid.systemId, datap->group_gid.localId, datap->group_gid.serial,
          PGUID (sedp_wr->e.guid)));
#endif
  return write_sample (NULL, sedp_wr, serdata);
}

static void handle_SEDP_GROUP_alive (nn_plist_t *datap /* note: potentially modifies datap */)
{
#define E(msg, lbl) do { nn_log (LC_TRACE, (msg)); goto lbl; } while (0)
  nn_guid_t ppguid;

  if (!(datap->present & PP_GROUP_GUID))
    E (" no guid?\n", err);
  TRACE ((" %x:%x:%x:%x", PGUID (datap->group_guid)));

  ppguid.prefix = datap->group_guid.prefix;
  ppguid.entityid.u = NN_ENTITYID_PARTICIPANT;
  if (ephash_lookup_proxy_participant_guid (&ppguid) == NULL)
    E (" unknown proxy pp?\n", err);

  TRACE ((" alive\n"));

  {
    struct v_gid_s *gid = NULL;
    char *name;
#if ! LITE
    if (datap->present & PP_PRISMTECH_GROUP_GID)
    {
      gid = &datap->group_gid;
    }
#endif
    name = (datap->present & PP_ENTITY_NAME) ? datap->entity_name : "";
    new_proxy_group (&datap->group_guid, gid, name, &datap->qos);
  }
err:
  return;
#undef E
}

static void handle_SEDP_GROUP_dead (nn_plist_t *datap)
{
  if (!(datap->present & PP_GROUP_GUID))
  {
    TRACE ((" no guid?\n"));
    return;
  }
  TRACE ((" %x:%x:%x:%x\n", PGUID (datap->group_guid)));
  delete_proxy_group (&datap->group_guid, 0);
}

static void handle_SEDP_GROUP (const struct receiver_state *rst, unsigned statusinfo, const void *vdata, unsigned len)
{
  const struct CDRHeader *data = vdata; /* built-ins not deserialized (yet) */
  TRACE (("SEDP_GROUP ST%x", statusinfo));
  if (data == NULL)
  {
    TRACE ((" no payload?\n"));
    return;
  }
  else
  {
    nn_plist_t decoded_data;
    nn_plist_src_t src;
    src.protocol_version = rst->protocol_version;
    src.vendorid = rst->vendor;
    src.encoding = data->identifier;
    src.buf = (unsigned char *) data + 4;
    src.bufsz = len - 4;
    if (nn_plist_init_frommsg (&decoded_data, NULL, ~(uint64_t)0, ~(uint64_t)0, &src) < 0)
    {
      NN_WARNING2 ("SEDP_GROUP (vendor %d.%d): invalid qos/parameters\n", src.vendorid.id[0], src.vendorid.id[1]);
      return;
    }

    switch (statusinfo & (NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER))
    {
      case 0:
        handle_SEDP_GROUP_alive (&decoded_data);
        break;

      case NN_STATUSINFO_DISPOSE:
      case NN_STATUSINFO_UNREGISTER:
      case (NN_STATUSINFO_DISPOSE | NN_STATUSINFO_UNREGISTER):
        handle_SEDP_GROUP_dead (&decoded_data);
        break;
    }

    nn_plist_fini (&decoded_data);
  }
}

/******************************************************************************
 *****************************************************************************/

/* FIXME: defragment is a copy of the one in q_receive.c, but the deserialised should be enhanced to handle fragmented data (and arguably the processing here should be built on proper data readers) */
static int defragment (unsigned char **datap, const struct nn_rdata *fragchain, uint32_t sz)
{
  if (fragchain->nextfrag == NULL)
  {
    *datap = NN_RMSG_PAYLOADOFF (fragchain->rmsg, NN_RDATA_PAYLOAD_OFF (fragchain));
    return 0;
  }
  else
  {
    unsigned char *buf;
    uint32_t off = 0;
    buf = os_malloc (sz);
    while (fragchain)
    {
      assert (fragchain->min <= off);
      assert (fragchain->maxp1 <= sz);
      if (fragchain->maxp1 > off)
      {
        /* only copy if this fragment adds data */
        const unsigned char *payload = NN_RMSG_PAYLOADOFF (fragchain->rmsg, NN_RDATA_PAYLOAD_OFF (fragchain));
        memcpy (buf + off, payload + off - fragchain->min, fragchain->maxp1 - off);
        off = fragchain->maxp1;
      }
      fragchain = fragchain->nextfrag;
    }
    *datap = buf;
    return 1;
  }
}

int builtins_dqueue_handler (const struct nn_rsample_info *sampleinfo, const struct nn_rdata *fragchain, UNUSED_ARG (const nn_guid_t *rdguid), UNUSED_ARG (void *qarg))
{
  struct proxy_writer *pwr;
  struct {
    struct CDRHeader cdr;
    nn_parameter_t p_endpoint_guid;
    char kh[16];
    nn_parameter_t p_sentinel;
  } keyhash_payload;
  unsigned statusinfo;
  int need_keyhash;
  nn_guid_t srcguid;
  Data_DataFrag_common_t *msg;
  unsigned char data_smhdr_flags;
  nn_plist_t qos;
  unsigned char *datap;
  int needs_free;
  uint32_t datasz = sampleinfo->size;
  needs_free = defragment (&datap, fragchain, sampleinfo->size);

  /* Luckily, most of the Data and DataFrag headers are the same - and
     in particular, all that we care about here is the same.  The
     key/data flags of DataFrag are different from those of Data, but
     DDSI2 used to treat them all as if they are data :( so now,
     instead of splitting out all the code, we reformat these flags
     from the submsg to always conform to that of the "Data"
     submessage regardless of the input. */
  msg = (Data_DataFrag_common_t *) NN_RMSG_PAYLOADOFF (fragchain->rmsg, NN_RDATA_SUBMSG_OFF (fragchain));
  data_smhdr_flags = normalize_data_datafrag_flags (&msg->smhdr, config.buggy_datafrag_flags_mode);
  srcguid.prefix = sampleinfo->rst->src_guid_prefix;
  srcguid.entityid = msg->writerId;

  pwr = sampleinfo->pwr;
  if (pwr == NULL)
    assert (srcguid.entityid.u == NN_ENTITYID_SPDP_BUILTIN_PARTICIPANT_WRITER);
  else
  {
    assert (is_builtin_entityid (pwr->e.guid.entityid, pwr->c.vendor));
    assert (memcmp (&pwr->e.guid, &srcguid, sizeof (srcguid)) == 0);
    assert (srcguid.entityid.u != NN_ENTITYID_SPDP_BUILTIN_PARTICIPANT_WRITER);
  }

  /* If there is no payload, it is either a completely invalid message
     or a dispose/unregister in RTI style. We assume the latter,
     consequently expect to need the keyhash.  Then, if sampleinfo
     says it is a complex qos, or the keyhash is required, extract all
     we need from the inline qos. */
  need_keyhash = (datasz == 0 || (data_smhdr_flags & (DATA_FLAG_KEYFLAG | DATA_FLAG_DATAFLAG)) == 0);
  if (!(sampleinfo->complex_qos || need_keyhash))
  {
    nn_plist_init_empty (&qos);
    statusinfo = sampleinfo->statusinfo;
  }
  else
  {
    nn_plist_src_t src;
    size_t qos_offset = NN_RDATA_SUBMSG_OFF (fragchain) + offsetof (Data_DataFrag_common_t, octetsToInlineQos) + sizeof (msg->octetsToInlineQos) + msg->octetsToInlineQos;
    src.protocol_version = sampleinfo->rst->protocol_version;
    src.vendorid = sampleinfo->rst->vendor;
    src.encoding = (msg->smhdr.flags & SMFLAG_ENDIANNESS) ? PL_CDR_LE : PL_CDR_BE;
    src.buf = NN_RMSG_PAYLOADOFF (fragchain->rmsg, qos_offset);
    src.bufsz = NN_RDATA_PAYLOAD_OFF (fragchain) - qos_offset;
    if (nn_plist_init_frommsg (&qos, NULL, PP_STATUSINFO | PP_KEYHASH, 0, &src) < 0)
    {
      NN_WARNING4 ("data(builtin, vendor %d.%d): %x:%x:%x:%x #%lld: invalid inline qos\n",
                   src.vendorid.id[0], src.vendorid.id[1], PGUID (srcguid),
                   (long long int) sampleinfo->seq);
      goto done_upd_deliv;
    }
    /* Complex qos bit also gets set when statusinfo bits other than
       dispose/unregister are set.  They are not currently defined,
       but this may save us if they do get defined one day. */
    statusinfo = (qos.present & PP_STATUSINFO) ? qos.statusinfo : 0;
  }

  if (pwr && ut_avlIsEmpty (&pwr->readers))
  {
    /* Wasn't empty when enqueued, but needn't still be; SPDP has no
       proxy writer, and is always accepted */
    goto done_upd_deliv;
  }

  /* Built-ins still do their own deserialization (SPDP <=> pwr ==
     NULL)). */
  assert (pwr == NULL || pwr->c.topic == NULL);
  if (statusinfo == 0)
  {
    if (datasz == 0 || !(data_smhdr_flags & DATA_FLAG_DATAFLAG))
    {
      NN_WARNING4 ("data(builtin, vendor %d.%d): %x:%x:%x:%x #%lld: "
                   "built-in data but no payload\n",
                   sampleinfo->rst->vendor.id[0], sampleinfo->rst->vendor.id[1],
                   PGUID (srcguid), (long long int) sampleinfo->seq);
      goto done_upd_deliv;
    }
  }
  else if (datasz)
  {
    /* Raw data must be full payload for write, just keys for
       dispose and unregister. First has been checked; the second
       hasn't been checked fully yet. */
    if (!(data_smhdr_flags & DATA_FLAG_KEYFLAG))
    {
      NN_WARNING4 ("data(builtin, vendor %d.%d): %x:%x:%x:%x #%lld: "
                   "dispose/unregister of built-in data but payload not just key\n",
                   sampleinfo->rst->vendor.id[0], sampleinfo->rst->vendor.id[1],
                   PGUID (srcguid), (long long int) sampleinfo->seq);
      goto done_upd_deliv;
    }
  }
  else if ((qos.present & PP_KEYHASH) && !NN_STRICT_P)
  {
    /* For SPDP/SEDP, fake a parameter list with just a keyhash.  For
       PMD, just use the keyhash directly.  Too hard to fix everything
       at the same time ... */
    if (srcguid.entityid.u == NN_ENTITYID_P2P_BUILTIN_PARTICIPANT_MESSAGE_WRITER)
    {
      datap = qos.keyhash.value;
      datasz = sizeof (qos.keyhash);
    }
    else
    {
      nn_parameterid_t pid;
      keyhash_payload.cdr.identifier = PLATFORM_IS_LITTLE_ENDIAN ? PL_CDR_LE : PL_CDR_BE;
      keyhash_payload.cdr.options = 0;
      switch (srcguid.entityid.u)
      {
        case NN_ENTITYID_SPDP_BUILTIN_PARTICIPANT_WRITER:
        case NN_ENTITYID_SEDP_BUILTIN_CM_PARTICIPANT_WRITER:
          pid = PID_PARTICIPANT_GUID;
          break;
        case NN_ENTITYID_SEDP_BUILTIN_CM_PUBLISHER_WRITER:
        case NN_ENTITYID_SEDP_BUILTIN_CM_SUBSCRIBER_WRITER:
          pid = PID_GROUP_GUID;
          break;
        case NN_ENTITYID_SEDP_BUILTIN_PUBLICATIONS_WRITER:
        case NN_ENTITYID_SEDP_BUILTIN_SUBSCRIPTIONS_WRITER:
          pid = PID_ENDPOINT_GUID;
          break;
        case NN_ENTITYID_SEDP_BUILTIN_TOPIC_WRITER:
        case NN_ENTITYID_P2P_BUILTIN_PARTICIPANT_MESSAGE_WRITER:
          /* placeholders */
          pid = PID_ENDPOINT_GUID;
          break;
        default:
          NN_WARNING4 ("data(builtin, vendor %d.%d): %x:%x:%x:%x #%lld: mapping keyhash to ENDPOINT_GUID",
                       sampleinfo->rst->vendor.id[0], sampleinfo->rst->vendor.id[1],
                       PGUID (srcguid), (long long int) sampleinfo->seq);
          pid = PID_ENDPOINT_GUID;
          break;
      }
      keyhash_payload.p_endpoint_guid.parameterid = pid;
      keyhash_payload.p_endpoint_guid.length = sizeof (nn_keyhash_t);
      memcpy (keyhash_payload.kh, &qos.keyhash, sizeof (qos.keyhash));
      keyhash_payload.p_sentinel.parameterid = PID_SENTINEL;
      keyhash_payload.p_sentinel.length = 0;
      datap = (unsigned char *) &keyhash_payload;
      datasz = sizeof (keyhash_payload);
    }
  }
  else
  {
    NN_WARNING4 ("data(builtin, vendor %d.%d): %x:%x:%x:%x #%lld: "
                 "dispose/unregister with no content\n",
                 sampleinfo->rst->vendor.id[0], sampleinfo->rst->vendor.id[1],
                 PGUID (srcguid), (long long int) sampleinfo->seq);
    goto done_upd_deliv;
  }

  switch (srcguid.entityid.u)
  {
    case NN_ENTITYID_SPDP_BUILTIN_PARTICIPANT_WRITER:
      handle_SPDP (sampleinfo->rst, statusinfo, datap, datasz);
      break;
    case NN_ENTITYID_SEDP_BUILTIN_PUBLICATIONS_WRITER:
    case NN_ENTITYID_SEDP_BUILTIN_SUBSCRIPTIONS_WRITER:
      handle_SEDP (sampleinfo->rst, statusinfo, datap, datasz);
      break;
    case NN_ENTITYID_P2P_BUILTIN_PARTICIPANT_MESSAGE_WRITER:
      handle_PMD (sampleinfo->rst, statusinfo, datap, datasz);
      break;
    case NN_ENTITYID_SEDP_BUILTIN_CM_PARTICIPANT_WRITER:
      handle_SEDP_CM (sampleinfo->rst, srcguid.entityid, statusinfo, datap, datasz);
      break;
    case NN_ENTITYID_SEDP_BUILTIN_CM_PUBLISHER_WRITER:
    case NN_ENTITYID_SEDP_BUILTIN_CM_SUBSCRIBER_WRITER:
      handle_SEDP_GROUP (sampleinfo->rst, statusinfo, datap, datasz);
      break;
#if ! LITE
    case NN_ENTITYID_SEDP_BUILTIN_TOPIC_WRITER:
      handle_SEDP_TOPIC (sampleinfo->rst, srcguid.entityid, statusinfo, datap, datasz);
      break;
#endif
    default:
      NN_WARNING4 ("data(builtin, vendor %d.%d): %x:%x:%x:%x #%lld: not handled\n",
                   sampleinfo->rst->vendor.id[0], sampleinfo->rst->vendor.id[1],
                   PGUID (srcguid), (long long int) sampleinfo->seq);
      break;
  }

 done_upd_deliv:
  if (needs_free)
    os_free (datap);
  if (pwr)
  {
    /* No proxy writer for SPDP */
    os_atomic_st32 (&pwr->next_deliv_seq_lowword, (uint32_t) (sampleinfo->seq + 1));
  }
  return 0;
}
