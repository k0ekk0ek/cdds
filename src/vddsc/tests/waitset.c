#include <assert.h>

#include "dds.h"
#include "os/os.h"
#include <criterion/criterion.h>
#include <criterion/logging.h>
#include <criterion/theories.h>
#include "RoundTrip.h"

/* Add --verbose command line argument to get the cr_log_info traces (if there are any). */

/**************************************************************************************************
 *
 * Some thread related convenience stuff.
 *
 *************************************************************************************************/

typedef enum thread_state_t {
    STARTING,
    WAITING,
    STOPPED
} thread_state_t;

typedef struct thread_arg_t {
    os_threadId    tid;
    thread_state_t state;
    dds_entity_t   expected;
} thread_arg_t;

static void         waiting_thread_start(struct thread_arg_t *arg, dds_entity_t expected);
static dds_return_t waiting_thread_expect_exit(struct thread_arg_t *arg);




/**************************************************************************************************
 *
 * Test fixtures
 *
 *************************************************************************************************/

static dds_entity_t participant = 0;
static dds_entity_t topic       = 0;
static dds_entity_t writer      = 0;
static dds_entity_t reader      = 0;
static dds_entity_t waitset     = 0;
static dds_entity_t publisher   = 0;
static dds_entity_t subscriber  = 0;
static dds_entity_t readcond    = 0;



static void
vddsc_waitset_basic_init(void)
{
    participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    cr_assert_gt(participant, 0, "Failed to create prerequisite participant");

    waitset = dds_create_waitset(participant);
    cr_assert_gt(waitset, 0, "Failed to create waitset");
}

static void
vddsc_waitset_basic_fini(void)
{
    /* It shouldn't matter if any of these entities were deleted previously.
     * dds_delete will just return an error, which we don't check. */
    dds_delete(waitset);
    dds_delete(participant);
}

static void
vddsc_waitset_init(void)
{
    uint32_t mask = DDS_ANY_SAMPLE_STATE | DDS_ANY_VIEW_STATE | DDS_ANY_INSTANCE_STATE;
    char name[100];

#if 1
    /* Get semi random topic name. */
    snprintf(name, 100,
            "vddsc_waitset_test_pid%"PRIprocId"_tid%d",
            os_procIdSelf(),
            (int)os_threadIdToInteger(os_threadIdSelf()));
#else
    /* Single topic name causes interference when tests are executed in parallel. */
    snprintf(name, 100, "%s", "vddsc_waitset_test");
#endif

    vddsc_waitset_basic_init();

    publisher = dds_create_publisher(participant, NULL, NULL);
    cr_assert_gt(publisher, 0, "Failed to create prerequisite publisher");

    subscriber = dds_create_subscriber(participant, NULL, NULL);
    cr_assert_gt(subscriber, 0, "Failed to create prerequisite subscriber");

    topic = dds_create_topic(participant, &RoundTripModule_DataType_desc, name, NULL, NULL);
    cr_assert_gt(topic, 0, "Failed to create prerequisite topic");

    reader = dds_create_reader(subscriber, topic, NULL, NULL);
    cr_assert_gt(reader, 0, "Failed to create prerequisite reader");

    writer = dds_create_writer(publisher, topic, NULL, NULL);
    cr_assert_gt(writer, 0, "Failed to create prerequisite writer");

    readcond = dds_create_readcondition(reader, mask);
    cr_assert_gt(readcond, 0, "Failed to create prerequisite publisher");
}

static void
vddsc_waitset_fini(void)
{
    /* It shouldn't matter if any of these entities were deleted previously.
     * dds_delete will just return an error, which we don't check. */
    dds_delete(readcond);
    dds_delete(writer);
    dds_delete(reader);
    dds_delete(topic);
    dds_delete(publisher);
    dds_delete(subscriber);
    vddsc_waitset_basic_fini();
}

static void
vddsc_waitset_attached_init(void)
{
    dds_return_t ret;

    vddsc_waitset_init();

    /* Start with interest in nothing. */
    ret = dds_set_enabled_status(participant, 0);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to remove prerequisite participant status");
    ret = dds_set_enabled_status(writer, 0);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to remove prerequisite writer status");
    ret = dds_set_enabled_status(reader, 0);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to remove prerequisite reader status");
    ret = dds_set_enabled_status(topic, 0);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to remove prerequisite topic status");
    ret = dds_set_enabled_status(publisher, 0);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to remove prerequisite publisher status");
    ret = dds_set_enabled_status(subscriber, 0);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to remove prerequisite subscriber status");

    /* Attach all entities to the waitset. */
    ret = dds_waitset_attach(waitset, participant, (dds_attach_t)(intptr_t)participant);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to attach prerequisite participant");
    ret = dds_waitset_attach(waitset, waitset, (dds_attach_t)(intptr_t)waitset);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to attach prerequisite waitset");
    ret = dds_waitset_attach(waitset, writer, (dds_attach_t)(intptr_t)writer);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to attach prerequisite writer");
    ret = dds_waitset_attach(waitset, reader, (dds_attach_t)(intptr_t)reader);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to attach prerequisite reader");
    ret = dds_waitset_attach(waitset, topic, (dds_attach_t)(intptr_t)topic);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to attach prerequisite topic");
    ret = dds_waitset_attach(waitset, publisher, (dds_attach_t)(intptr_t)publisher);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to attach prerequisite publisher");
    ret = dds_waitset_attach(waitset, subscriber, (dds_attach_t)(intptr_t)subscriber);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to attach prerequisite subscriber");

}

static void
vddsc_waitset_attached_fini(void)
{
    /* Detach all entities to the waitset. */
    dds_waitset_detach(waitset, participant);
    dds_waitset_detach(waitset, topic);
    dds_waitset_detach(waitset, publisher);
    dds_waitset_detach(waitset, subscriber);
    dds_waitset_detach(waitset, waitset);
    dds_waitset_detach(waitset, writer);
    dds_waitset_detach(waitset, reader);

    vddsc_waitset_fini();
}

#if 0
#else
/**************************************************************************************************
 *
 * These will check the waitset creation in various ways.
 *
 *************************************************************************************************/
/*************************************************************************************************/
Test(vddsc_waitset_create, second, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_entity_t ws;
    dds_return_t ret;

    /* Basically, vddsc_waitset_basic_init() already tested the creation of a waitset. But
     * just see if we can create a second one. */
    ws = dds_create_waitset(participant);
    cr_assert_gt(ws, 0, "dds_create_waitset(): returned %d", dds_err_nr(ws));

    /* Also, we should be able to delete this second one. */
    ret = dds_delete(ws);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_delete(): returned %d", dds_err_nr(ret));

    /* And, of course, be able to delete the first one (return code isn't checked in the test fixtures). */
    ret = dds_delete(waitset);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_delete(): returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_create, deleted_participant, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_entity_t ws;
    dds_entity_t deleted;
    deleted = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    dds_delete(deleted);
    ws = dds_create_waitset(deleted);
    cr_assert_eq(dds_err_nr(ws), DDS_RETCODE_ALREADY_DELETED, "dds_create_waitset(): returned %d", dds_err_nr(ws));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_create, invalid_params) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t par), vddsc_waitset_create, invalid_params, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_entity_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_entity_t ws;

    if (par < 0) {
        /* Entering the API with an error should return the same error. */
        exp = par;
    }

    ws = dds_create_waitset(par);
    cr_assert_eq(dds_err_nr(ws), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(ws), dds_err_nr(exp));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_create, non_participants) = {
        DataPoints(dds_entity_t*, &topic, &writer, &reader, &waitset, &publisher, &subscriber, &readcond),
};
Theory((dds_entity_t *par), vddsc_waitset_create, non_participants, .init=vddsc_waitset_init, .fini=vddsc_waitset_fini)
{
    dds_entity_t ws;
    ws = dds_create_waitset(*par);
    cr_assert_eq(dds_err_nr(ws), DDS_RETCODE_ILLEGAL_OPERATION, "returned %d", dds_err_nr(ws));
}
/*************************************************************************************************/



/**************************************************************************************************
 *
 * These will check the waitset attach in various invalid ways.
 *   - Valid waitset but try to attach all kinds of invalid entities.
 *   - Try to attach a valid participant to all kinds of invalid entities.
 *   - Try attaching valid entities to valid (non-waitset) entities
 *   - Try attaching a valid entity a second time.
 *
 *************************************************************************************************/
/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_attach, invalid_params) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
        DataPoints(dds_attach_t,  NULL, &reader, (dds_attach_t)3),
};
Theory((dds_entity_t e, dds_attach_t a), vddsc_waitset_attach, invalid_params, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t ret;
    ret = dds_waitset_attach(waitset, e, a);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_BAD_PARAMETER, "returned %d != expected %d", dds_err_nr(ret), DDS_RETCODE_BAD_PARAMETER);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_attach, invalid_waitsets) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
        DataPoints(dds_attach_t,  NULL, &reader, (dds_attach_t)3),
};
Theory((dds_entity_t ws, dds_attach_t a), vddsc_waitset_attach, invalid_waitsets, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_return_t ret;

    if (ws < 0) {
        /* Entering the API with an error should return the same error. */
        exp = ws;
    }

    ret = dds_waitset_attach(ws, participant, a);
    cr_assert_eq(dds_err_nr(ret), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(ret), dds_err_nr(exp));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_attach, non_waitsets) = {
        DataPoints(dds_entity_t*, &participant, &topic, &writer, &reader,           &publisher, &subscriber, &readcond),
        DataPoints(dds_entity_t*, &participant, &topic, &writer, &reader, &waitset, &publisher, &subscriber, &readcond),
        DataPoints(dds_attach_t,  NULL, &reader, (dds_attach_t)3),
};
Theory((dds_entity_t *ws, dds_entity_t *e, dds_attach_t a), vddsc_waitset_attach, non_waitsets, .init=vddsc_waitset_init, .fini=vddsc_waitset_fini)
{
    dds_return_t ret;
    ret = dds_waitset_attach(*ws, *e, a);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ILLEGAL_OPERATION, "returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_attach, deleted_waitset, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t ret;
    dds_delete(waitset);
    ret = dds_waitset_attach(waitset, participant, NULL);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED, "returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/



/**************************************************************************************************
 *
 * These will check the waitset attach and detach with valid entities (which should always work).
 *
 *************************************************************************************************/
/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_attach_detach, valid_entities) = {
        DataPoints(dds_entity_t*, &participant, &topic, &writer, &reader, &waitset, &publisher, &subscriber, &readcond),
        DataPoints(dds_entity_t*, &participant, &topic, &writer, &reader, &waitset, &publisher, &subscriber, &readcond),
        DataPoints(dds_attach_t,  NULL, &reader, (dds_attach_t)3),
};
Theory((dds_entity_t *ws, dds_entity_t *e, dds_attach_t a), vddsc_waitset_attach_detach, valid_entities, .init=vddsc_waitset_init, .fini=vddsc_waitset_fini)
{
    dds_return_t exp;
    dds_return_t ret;

    if (*ws == waitset) {
        /* Attaching to the waitset should work. */
        exp = DDS_RETCODE_OK;
    } else {
        /* Attaching to every other entity should fail. */
        exp = DDS_RETCODE_ILLEGAL_OPERATION;
    }

    /* Try to attach. */
    ret = dds_waitset_attach(*ws, *e, a);
    cr_assert_eq(dds_err_nr(ret), exp, "returned %d != expected %d", dds_err_nr(ret), exp);

    /* Detach when needed. */
    if (ret == DDS_RETCODE_OK) {
        ret = dds_waitset_detach(*ws, *e);
        cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_detach(): returned %d", dds_err_nr(ret));
    }
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_attach_detach, second, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t ret;

    ret = dds_waitset_attach(waitset, waitset, NULL);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_attach(): returned %d", dds_err_nr(ret));

    ret = dds_waitset_attach(waitset, waitset, NULL);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_PRECONDITION_NOT_MET, "dds_waitset_attach(): returned %d", dds_err_nr(ret));

    ret = dds_waitset_detach(waitset, waitset);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_detach(): returned %d", dds_err_nr(ret));

    ret = dds_waitset_detach(waitset, waitset);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_PRECONDITION_NOT_MET, "dds_waitset_detach(): returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/



/**************************************************************************************************
 *
 * These will check the waitset detach in various ways.
 *   - Valid waitset but try to detach all kinds of invalid entities.
 *   - Try to detach a valid participant from all kinds of invalid entities.
 *   - Try detach valid entities from valid entities
 *          -- detach to entities that are not waitsets.
 *          -- detach to a waitset.
 *
 * Note: everything is expected to fail for varous reasons. The 'happy day' detach is tested on
 *       the fly in vddsc_waitset_attach_detach::valid_entities
 *
 *************************************************************************************************/
/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_detach, invalid_params) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t e), vddsc_waitset_detach, invalid_params, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t ret;
    ret = dds_waitset_detach(waitset, e);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_BAD_PARAMETER, "returned %d != expected %d", dds_err_nr(ret), DDS_RETCODE_BAD_PARAMETER);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_detach, invalid_waitsets) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t ws), vddsc_waitset_detach, invalid_waitsets, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_return_t ret;

    if (ws < 0) {
        /* Entering the API with an error should return the same error. */
        exp = ws;
    }

    ret = dds_waitset_detach(ws, participant);
    cr_assert_eq(dds_err_nr(ret), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(ret), dds_err_nr(exp));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_detach, valid_entities) = {
        DataPoints(dds_entity_t*, &participant, &topic, &writer, &reader, &waitset, &publisher, &subscriber, &readcond),
        DataPoints(dds_entity_t*, &participant, &topic, &writer, &reader, &waitset, &publisher, &subscriber, &readcond),
};
Theory((dds_entity_t *ws, dds_entity_t *e), vddsc_waitset_detach, valid_entities, .init=vddsc_waitset_init, .fini=vddsc_waitset_fini)
{
    dds_return_t exp;
    dds_return_t ret;

    if (*ws == waitset) {
        /* Detaching from an empty waitset should yield 'precondition not met'. */
        exp = DDS_RETCODE_PRECONDITION_NOT_MET;
    } else {
        /* Attaching to every other entity should yield 'illegal operation'. */
        exp = DDS_RETCODE_ILLEGAL_OPERATION;
    }

    ret = dds_waitset_detach(*ws, *e);
    cr_assert_eq(dds_err_nr(ret), exp, "returned %d != expected %d", dds_err_nr(ret), exp);
}
/*************************************************************************************************/




/**************************************************************************************************
 *
 * These will check the waitset attach/detach in various ways. We will use NULL as attach argument
 * because it will be properly used in 'waitset behaviour' tests anyway.
 *
 *************************************************************************************************/
/*************************************************************************************************/
Test(vddsc_waitset_attach_detach, itself, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t ret;

    ret = dds_waitset_attach(waitset, waitset, NULL);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_attach(): returned %d", dds_err_nr(ret));

    ret = dds_waitset_detach(waitset, waitset);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_detach(): returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_attach_detach, participant, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t ret;

    ret = dds_waitset_attach(waitset, participant, NULL);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_attach(): returned %d", dds_err_nr(ret));

    ret = dds_waitset_detach(waitset, participant);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_detach(): returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_attach_detach, reader, .init=vddsc_waitset_init, .fini=vddsc_waitset_fini)
{
    dds_return_t ret;

    ret = dds_waitset_attach(waitset, reader, NULL);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_attach(): returned %d", dds_err_nr(ret));

    ret = dds_waitset_detach(waitset, reader);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_detach(): returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_attach_detach, readcondition, .init=vddsc_waitset_init, .fini=vddsc_waitset_fini)
{
    dds_return_t ret;

    ret = dds_waitset_attach(waitset, readcond, NULL);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_attach(): returned %d", dds_err_nr(ret));

    ret = dds_waitset_detach(waitset, readcond);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_detach(): returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/




/**************************************************************************************************
 *
 * These will check entities can be deleted while attached to the waitset. We will use NULL as
 * attach argument because it will be properly used in 'waitset behaviour' tests anyway.
 * 1) Test if the waitset can be deleted when attached to itself.
 * 2) Test if the waitset parent can be deleted when attached.
 * 3) Test if an 'ordinary' entity can be deleted when attached.
 * 4) Test if an condition can be deleted when attached.
 *
 *************************************************************************************************/
/*************************************************************************************************/
Test(vddsc_waitset_delete_attached, self, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t ret;

    ret = dds_waitset_attach(waitset, waitset, NULL);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_attach(): returned %d", dds_err_nr(ret));

    ret = dds_delete(waitset);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_delete(): returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_delete_attached, participant, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t ret;

    ret = dds_waitset_attach(waitset, participant, NULL);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_attach(): returned %d", dds_err_nr(ret));

    ret = dds_delete(participant);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_delete(): returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_delete_attached, reader, .init=vddsc_waitset_init, .fini=vddsc_waitset_fini)
{
    dds_return_t ret;

    ret = dds_waitset_attach(waitset, reader, NULL);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_attach(): returned %d", dds_err_nr(ret));

    ret = dds_delete(reader);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_delete(): returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_delete_attached, readcondition, .init=vddsc_waitset_init, .fini=vddsc_waitset_fini)
{
    dds_return_t ret;

    ret = dds_waitset_attach(waitset, readcond, NULL);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_attach(): returned %d", dds_err_nr(ret));

    ret = dds_delete(readcond);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_delete(): returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/



/**************************************************************************************************
 *
 * These will check the waitset triggering in various invalid ways.
 * The happy days scenario is tested by vddsc_waitset_triggering::on_self.
 *
 *************************************************************************************************/
/*************************************************************************************************/
Test(vddsc_waitset_set_trigger, deleted_waitset, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t ret;
    dds_delete(waitset);
    ret = dds_waitset_set_trigger(waitset, true);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED, "returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_set_trigger, invalid_params) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t ws), vddsc_waitset_set_trigger, invalid_params, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_return_t ret;

    if (ws < 0) {
        /* Entering the API with an error should return the same error. */
        exp = ws;
    }

    ret = dds_waitset_set_trigger(ws, true);
    cr_assert_eq(dds_err_nr(ret), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(ret), dds_err_nr(exp));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_set_trigger, non_waitsets) = {
        DataPoints(dds_entity_t*, &participant, &topic, &writer, &reader, &publisher, &subscriber, &readcond),
};
Theory((dds_entity_t *ws), vddsc_waitset_set_trigger, non_waitsets, .init=vddsc_waitset_init, .fini=vddsc_waitset_fini)
{
    dds_return_t ret;
    ret = dds_waitset_set_trigger(*ws, true);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ILLEGAL_OPERATION, "returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/



/**************************************************************************************************
 *
 * These will check the waitset wait in various invalid ways.
 * The happy days scenario is tested by vddsc_waitset_triggering::*.
 *
 *************************************************************************************************/
/*************************************************************************************************/
Test(vddsc_waitset_wait, deleted_waitset, .init=vddsc_waitset_attached_init, .fini=vddsc_waitset_attached_fini)
{
    dds_attach_t triggered;
    dds_return_t ret;
    dds_delete(waitset);
    ret = dds_waitset_wait(waitset, &triggered, 1, DDS_SECS(1));
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED, "returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_wait, invalid_waitsets) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t ws), vddsc_waitset_wait, invalid_waitsets, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_attach_t triggered;
    dds_return_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_return_t ret;

    if (ws < 0) {
        /* Entering the API with an error should return the same error. */
        exp = ws;
    }

    ret = dds_waitset_wait(ws, &triggered, 1, DDS_SECS(1));
    cr_assert_eq(dds_err_nr(ret), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(ret), dds_err_nr(exp));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_wait, non_waitsets) = {
        DataPoints(dds_entity_t*, &participant, &topic, &writer, &reader, &publisher, &subscriber, &readcond),
};
Theory((dds_entity_t *ws), vddsc_waitset_wait, non_waitsets, .init=vddsc_waitset_init, .fini=vddsc_waitset_fini)
{
    dds_attach_t triggered;
    dds_return_t ret;
    ret = dds_waitset_wait(*ws, &triggered, 1, DDS_SECS(1));
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ILLEGAL_OPERATION, "returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
static dds_attach_t attachment;
TheoryDataPoints(vddsc_waitset_wait, invalid_params) = {
        DataPoints(dds_attach_t *, &attachment, NULL),
        DataPoints(size_t, 0, 1, 100),
        DataPoints(int, -1, 0, 1),
};
Theory((dds_attach_t *a, size_t size, int msec), vddsc_waitset_wait, invalid_params, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_return_t ret;

    /* Only test when the combination of parameters is actually invalid. */
    cr_assume((a == NULL) || (size <= 0) || (msec < 0));

    ret = dds_waitset_wait(waitset, a, size, DDS_MSECS(msec));
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_BAD_PARAMETER, "returned %d != expected %d", dds_err_nr(ret), DDS_RETCODE_BAD_PARAMETER);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_wait_until, deleted_waitset, .init=vddsc_waitset_attached_init, .fini=vddsc_waitset_attached_fini)
{
    dds_attach_t triggered;
    dds_return_t ret;
    dds_delete(waitset);
    ret = dds_waitset_wait_until(waitset, &triggered, 1, dds_time());
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED, "returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_wait_until, invalid_waitsets) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t ws), vddsc_waitset_wait_until, invalid_waitsets, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_attach_t triggered;
    dds_return_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_return_t ret;

    if (ws < 0) {
        /* Entering the API with an error should return the same error. */
        exp = ws;
    }

    ret = dds_waitset_wait_until(ws, &triggered, 1, dds_time());
    cr_assert_eq(dds_err_nr(ret), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(ret), dds_err_nr(exp));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_wait_until, non_waitsets) = {
        DataPoints(dds_entity_t*, &participant, &topic, &writer, &reader, &publisher, &subscriber, &readcond),
};
Theory((dds_entity_t *ws), vddsc_waitset_wait_until, non_waitsets, .init=vddsc_waitset_init, .fini=vddsc_waitset_fini)
{
    dds_attach_t triggered;
    dds_return_t ret;
    ret = dds_waitset_wait_until(*ws, &triggered, 1, dds_time());
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ILLEGAL_OPERATION, "returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
static dds_attach_t attachment;
TheoryDataPoints(vddsc_waitset_wait_until, invalid_params) = {
        DataPoints(dds_attach_t *, &attachment, NULL),
        DataPoints(size_t, 0, 1, 100)
};
Theory((dds_attach_t *a, size_t size), vddsc_waitset_wait_until, invalid_params, .init=vddsc_waitset_basic_init, .fini=vddsc_waitset_basic_fini)
{
    dds_return_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_return_t ret;

    /* Only test when the combination of parameters is actually invalid. */
    cr_assume((a == NULL) || (size <= 0));

    ret = dds_waitset_wait_until(waitset, a, size, dds_time());
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_BAD_PARAMETER, "returned %d != expected %d", dds_err_nr(ret), DDS_RETCODE_BAD_PARAMETER);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_wait_until, past, .init=vddsc_waitset_attached_init, .fini=vddsc_waitset_attached_fini)
{
    dds_attach_t triggered;
    dds_return_t ret;

    ret = dds_waitset_wait_until(waitset, &triggered, 1, dds_time() - 100000);
    cr_assert_eq(ret, 0, "returned %d != expected 0", ret);
}
/*************************************************************************************************/



/**************************************************************************************************
 *
 * These will check the waitset getting the attached entities.
 *
 *************************************************************************************************/
#define MAX_ENTITIES_CNT    (10)
#define FOUND_PARTICIPANT   (0x0001)
#define FOUND_PUBLISHER     (0x0002)
#define FOUND_SUBSCRIBER    (0x0004)
#define FOUND_WAITSET       (0x0008)
#define FOUND_TOPIC         (0x0010)
#define FOUND_READER        (0x0020)
#define FOUND_WRITER        (0x0040)
#define FOUND_ALL           (0x007F)

static uint32_t NumberOfSetBits(uint32_t i)
{
    /* https://stackoverflow.com/questions/109023/how-to-count-the-number-of-set-bits-in-a-32-bit-integer */
    // Java: use >>> instead of >>
    // C or C++: use uint32_t
    i = i - ((i >> 1) & 0x55555555);
    i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
    return (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_get_entities, array_sizes) = {
        DataPoints(size_t, 0, 1, 7, MAX_ENTITIES_CNT),
};
Theory((size_t size), vddsc_waitset_get_entities, array_sizes, .init=vddsc_waitset_attached_init, .fini=vddsc_waitset_attached_fini)
{
    uint32_t found = 0;
    dds_return_t i;
    dds_return_t ret;
    dds_entity_t entities[MAX_ENTITIES_CNT];

    /* Make sure at least one entity is in the waitsets' internal triggered list. */
    ret = dds_waitset_set_trigger(waitset, true);
    cr_assert_eq(ret, DDS_RETCODE_OK, "Failed to prerequisite trigger entity");

    /* Get the actual attached entities. */
    ret = dds_waitset_get_entities(waitset, entities, size);

    /* vddsc_waitset_attached_init() attached 7 entities. */
    cr_assert_eq(ret, 7, "entities cnt %d (err %d)", ret, dds_err_nr(ret));

    /* Found entities should be only present once. */
    ret = ((dds_return_t)size < ret) ? (dds_return_t)size : ret;
    for (i = 0; i < ret; i++) {
        if (entities[i] == participant) {
            cr_assert(!(found & FOUND_PARTICIPANT), "Participant found twice");
            found |= FOUND_PARTICIPANT;
        } else if (entities[i] == publisher) {
            cr_assert(!(found & FOUND_PUBLISHER), "Publisher found twice");
            found |= FOUND_PUBLISHER;
        } else if (entities[i] == subscriber) {
            cr_assert(!(found & FOUND_SUBSCRIBER), "Subscriber found twice");
            found |= FOUND_SUBSCRIBER;
        } else if (entities[i] == waitset) {
            cr_assert(!(found & FOUND_WAITSET), "Waitset found twice");
            found |= FOUND_WAITSET;
        } else if (entities[i] == topic) {
            cr_assert(!(found & FOUND_TOPIC), "Topic found twice");
            found |= FOUND_TOPIC;
        } else if (entities[i] == reader) {
            cr_assert(!(found & FOUND_READER), "Reader found twice");
            found |= FOUND_READER;
        } else if (entities[i] == writer) {
            cr_assert(!(found & FOUND_WRITER), "Writer found twice");
            found |= FOUND_WRITER;
        }
    }

    /* Every found entity should be a known one. */
    cr_assert_eq(NumberOfSetBits(found), ret, "Not all found entities are known");
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_get_entities, no_array, .init=vddsc_waitset_attached_init, .fini=vddsc_waitset_attached_fini)
{
    dds_return_t ret;
    ret = dds_waitset_get_entities(waitset, NULL, 1);

    /* vddsc_waitset_attached_init attached 7 entities. */
    cr_assert_eq(ret, 7, "entities cnt %d (err %d)", ret, dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_waitset_get_entities, deleted_waitset, .init=vddsc_waitset_attached_init, .fini=vddsc_waitset_attached_fini)
{
    uint32_t found = 0;
    dds_return_t ret;
    dds_entity_t entities[MAX_ENTITIES_CNT];
    dds_delete(waitset);
    ret = dds_waitset_get_entities(waitset, entities, MAX_ENTITIES_CNT);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED, "returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_get_entities, invalid_params) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t ws), vddsc_waitset_get_entities, invalid_params, .init=vddsc_waitset_attached_init, .fini=vddsc_waitset_attached_fini)
{
    dds_return_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_entity_t entities[MAX_ENTITIES_CNT];
    dds_return_t ret;

    if (ws < 0) {
        /* Entering the API with an error should return the same error. */
        exp = ws;
    }

    ret = dds_waitset_get_entities(ws, entities, MAX_ENTITIES_CNT);
    cr_assert_eq(dds_err_nr(ret), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(ret), dds_err_nr(exp));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_waitset_get_entities, non_waitsets) = {
        DataPoints(dds_entity_t*, &participant, &topic, &writer, &reader, &publisher, &subscriber, &readcond),
};
Theory((dds_entity_t *ws), vddsc_waitset_get_entities, non_waitsets, .init=vddsc_waitset_attached_init, .fini=vddsc_waitset_attached_fini)
{
    dds_entity_t entities[MAX_ENTITIES_CNT];
    dds_return_t ret;
    ret = dds_waitset_get_entities(*ws, entities, MAX_ENTITIES_CNT);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ILLEGAL_OPERATION, "returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/





/**************************************************************************************************
 *
 * This will check if waitset will wake up when it is attached to itself and triggered.
 *
 * In short:
 * 1) Attach the waitset to itself
 * 2) A different thread will call dds_waitset_wait. This should block because the waitset
 *    hasn't been triggered yet. We also want it to block to know for sure that it'll wake up.
 * 3) Trigger the waitset. This should unblock the other thread that was waiting on the waitset.
 * 4) A new dds_waitset_wait should return immediately because the trigger value hasn't been
 *    reset (dds_waitset_set_trigger(waitset, false)) after the waitset woke up.
 *
 *************************************************************************************************/
/*************************************************************************************************/
Test(vddsc_waitset_triggering, on_self, .init=vddsc_waitset_attached_init, .fini=vddsc_waitset_attached_fini)
{
    dds_attach_t triggered;
    thread_arg_t arg;
    dds_return_t ret;

    /* The waitset should not have been triggered. */
    ret = dds_triggered(waitset);
    cr_assert_eq(ret, 0, "dds_triggered(): returned %d", dds_err_nr(ret));

    /* Start a thread that'll wait because no entity attached to the waitset
     * has been triggered (for instance by a status change). */
    waiting_thread_start(&arg, waitset);

    /* Triggering of the waitset should unblock the thread. */
    ret = dds_waitset_set_trigger(waitset, true);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_set_trigger(): returned %d", dds_err_nr(ret));
    ret = waiting_thread_expect_exit(&arg);
    cr_assert_eq(ret, DDS_RETCODE_OK, "waiting thread did not unblock");

    /* Now the trigger state should be true. */
    ret = dds_triggered(waitset);
    cr_assert_gt(ret, 0, "dds_triggered(): returned %d", dds_err_nr(ret));

    /* Waitset shouldn't wait, but immediately return our waitset. */
    ret = dds_waitset_wait(waitset, &triggered, 1, DDS_SECS(1));
    cr_assert_eq(ret, 1, "dds_waitset_wait(): returned %d", ret);
    cr_assert_eq(waitset, (dds_entity_t)(intptr_t)triggered);

    /* Reset waitset trigger. */
    ret = dds_waitset_set_trigger(waitset, false);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_set_trigger(): returned %d", dds_err_nr(ret));
    ret = dds_triggered(waitset);
    cr_assert_eq(ret, 0, "dds_triggered(): returned %d", dds_err_nr(ret));
}
/*************************************************************************************************/




/**************************************************************************************************
 *
 * This will check if waitset will wake up when data is written related to an attached reader.
 *
 * In short:
 * 1) Attach the reader to the waitset
 * 2) A different thread will call dds_waitset_wait. This should block because the reader
 *    hasn't got data yet. We also want it to block to know for sure that it'll wake up.
 * 3) Write data. This should unblock the other thread that was waiting on the waitset.
 * 4) A new dds_waitset_wait should return immediately because the status of the reader hasn't
 *    changed after the first trigger (it didn't read the data).
 *
 *************************************************************************************************/
/*************************************************************************************************/
Test(vddsc_waitset_triggering, on_reader, .init=vddsc_waitset_attached_init, .fini=vddsc_waitset_attached_fini)
{
    RoundTripModule_DataType sample;
    dds_attach_t triggered;
    thread_arg_t arg;
    dds_return_t ret;

    memset(&sample, 0, sizeof(RoundTripModule_DataType));

    /* Only interested in data_available for this test. */
    ret = dds_set_enabled_status(reader, DDS_DATA_AVAILABLE_STATUS);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_set_enabled_status(): returned %d", dds_err_nr(ret));

    /* The reader should not have been triggered. */
    ret = dds_triggered(reader);
    cr_assert_eq(ret, 0, "dds_triggered(): returned %d", dds_err_nr(ret));

    /* Start a thread that'll wait because no entity attached to the waitset
     * has been triggered (for instance by a status change). */
    waiting_thread_start(&arg, reader);

    /* Writing data should unblock the thread. */
    ret = dds_write(writer, &sample);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_write(): returned %d", dds_err_nr(ret));
    ret = waiting_thread_expect_exit(&arg);
    cr_assert_eq(ret, DDS_RETCODE_OK, "waiting thread did not unblock");

    /* Now the trigger state should be true. */
    ret = dds_triggered(reader);
    cr_assert_gt(ret, 0, "dds_triggered: Invalid return code %d", dds_err_nr(ret));

    /* Waitset shouldn't wait, but immediately return our reader. */
    ret = dds_waitset_wait(waitset, &triggered, 1, DDS_SECS(1));
    cr_assert_eq(ret, 1, "dds_waitset_wait ret");
    cr_assert_eq(reader, (dds_entity_t)(intptr_t)triggered, "dds_waitset_wait attachment");
}
/*************************************************************************************************/




/**************************************************************************************************
 *
 * This will check if waitset will wake up when data is written related to an attached condition.
 *
 * In short:
 * 1) Attach the readcondition to the waitset
 * 2) A different thread will call dds_waitset_wait. This should block because the related reader
 *    hasn't got data yet. We also want it to block to know for sure that it'll wake up.
 * 3) Write data. This should unblock the other thread that was waiting on the readcondition.
 * 4) A new dds_waitset_wait should return immediately because the status of the related reader
 *    (and thus the readcondition) hasn't changed after the first trigger (it didn't read the data).
 *
 *************************************************************************************************/
/*************************************************************************************************/
Test(vddsc_waitset_triggering, on_readcondition, .init=vddsc_waitset_attached_init, .fini=vddsc_waitset_attached_fini)
{
    RoundTripModule_DataType sample;
    dds_attach_t triggered;
    thread_arg_t arg;
    dds_return_t ret;

    memset(&sample, 0, sizeof(RoundTripModule_DataType));

    /* Make sure that we start un-triggered. */
    ret = dds_triggered(readcond);
    cr_assert_eq(ret, 0, "dds_triggered: Invalid return code %d", dds_err_nr(ret));

    /* Attach condition to the waitset. */
    ret = dds_waitset_attach(waitset, readcond, (dds_attach_t)(intptr_t)readcond);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_attach(): returned %d", dds_err_nr(ret));

    /* Start a thread that'll wait because no entity attached to the waitset
     * has been triggered (for instance by a status change). */
    waiting_thread_start(&arg, readcond);

    /* Writing data should unblock the thread. */
    ret = dds_write(writer, &sample);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_write(): returned %d", dds_err_nr(ret));
    ret = waiting_thread_expect_exit(&arg);
    cr_assert_eq(ret, DDS_RETCODE_OK, "waiting thread did not unblock");

    /* Now the trigger state should be true. */
    ret = dds_triggered(readcond);
    cr_assert_gt(ret, 0, "dds_triggered: Invalid return code %d", dds_err_nr(ret));

    /* Waitset shouldn't wait, but immediately return our reader. */
    ret = dds_waitset_wait(waitset, &triggered, 1, DDS_SECS(1));
    cr_assert_eq(ret, 1, "dds_waitset_wait ret");
    cr_assert_eq(readcond, (dds_entity_t)(intptr_t)triggered, "dds_waitset_wait attachment");

    /* Detach condition. */
    ret = dds_waitset_detach(waitset, readcond);
    cr_assert_eq(ret, DDS_RETCODE_OK, "dds_waitset_attach: Invalid return code %d", dds_err_nr(ret));
}
/*************************************************************************************************/





#endif

/**************************************************************************************************
 *
 * Convenience support functions.
 *
 *************************************************************************************************/

static uint32_t
waiting_thread(void *a)
{
    thread_arg_t *arg = (thread_arg_t*)a;
    dds_attach_t triggered;
    dds_return_t ret;

    arg->state = WAITING;
    /* This should block until the main test released all claims. */
    ret = dds_waitset_wait(waitset, &triggered, 1, DDS_SECS(1000));
    cr_assert_eq(ret, 1, "dds_waitset_wait returned %d", ret);
    cr_assert_eq(arg->expected, (dds_entity_t)(intptr_t)triggered, "dds_waitset_wait attachment");
    arg->state = STOPPED;

    return 0;
}

static os_result
thread_reached_state(thread_state_t *actual, thread_state_t expected, int32_t msec)
{
    /* Convenience function. */
    bool stopped = false;
    os_time msec10 = { 0, 10000000 };
    while ((msec > 0) && (*actual != expected)) {
        os_nanoSleep(msec10);
        msec -= 10;
    }
    return (*actual == expected) ? os_resultSuccess : os_resultTimeout;
}

static void
waiting_thread_start(struct thread_arg_t *arg, dds_entity_t expected)
{
    os_threadId   thread_id;
    os_threadAttr thread_attr;
    os_result     osr;

    assert(arg);

    /* Create an other thread that will blocking wait on the waitset. */
    arg->expected = expected;
    arg->state   = STARTING;
    os_threadAttrInit(&thread_attr);
    osr = os_threadCreate(&thread_id, "waiting_thread", &thread_attr, waiting_thread, arg);
    cr_assert_eq(osr, os_resultSuccess, "os_threadCreate");

    /* The thread should reach 'waiting' state. */
    osr = thread_reached_state(&(arg->state), WAITING, 1000);
    cr_assert_eq(osr, os_resultSuccess, "waiting returned %d", osr);

    /* But thread should block and thus NOT reach 'stopped' state. */
    osr = thread_reached_state(&(arg->state), STOPPED, 100);
    cr_assert_eq(osr, os_resultTimeout, "waiting returned %d", osr);

    arg->tid = thread_id;
}

static dds_return_t
waiting_thread_expect_exit(struct thread_arg_t *arg)
{
    os_result osr;
    assert(arg);
    osr = thread_reached_state(&(arg->state), STOPPED, 5000);
    if (osr == os_resultSuccess) {
        os_threadWaitExit(arg->tid, NULL);
        return DDS_RETCODE_OK;
    }
    return DDS_RETCODE_TIMEOUT;
}

