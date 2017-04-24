#include "dds.h"

int main (int argc, char *argv[])
{
  dds_entity_t participant;
  int status;

  status = dds_init (argc, argv);
  DDS_ERR_CHECK (status, DDS_CHECK_REPORT | DDS_CHECK_EXIT);
  status = dds_participant_create (&participant, DDS_DOMAIN_DEFAULT, NULL, NULL);
  DDS_ERR_CHECK (status, DDS_CHECK_REPORT | DDS_CHECK_EXIT);

  dds_entity_delete (participant);

  dds_fini ();

  return 0;
}