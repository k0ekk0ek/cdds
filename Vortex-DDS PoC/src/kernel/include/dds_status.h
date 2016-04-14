#ifndef _DDS_STATUS_H_
#define _DDS_STATUS_H_

#include "dds_types.h"

#if defined (__cplusplus)
extern "C" {
#endif

/* Status(s) supported on entity type */

#define DDS_TOPIC_STATUS_MASK DDS_INCONSISTENT_TOPIC_STATUS
#define DDS_READER_STATUS_MASK \
   DDS_SAMPLE_REJECTED_STATUS | \
   DDS_LIVELINESS_CHANGED_STATUS | \
   DDS_REQUESTED_DEADLINE_MISSED_STATUS | \
   DDS_REQUESTED_INCOMPATIBLE_QOS_STATUS | \
   DDS_DATA_AVAILABLE_STATUS | \
   DDS_SAMPLE_LOST_STATUS | \
   DDS_SUBSCRIPTION_MATCHED_STATUS 
#define DDS_WRITER_STATUS_MASK \
   DDS_LIVELINESS_LOST_STATUS | \
   DDS_OFFERED_DEADLINE_MISSED_STATUS | \
   DDS_OFFERED_INCOMPATIBLE_QOS_STATUS | \
   DDS_PUBLICATION_MATCHED_STATUS 
#define DDS_SUBSCRIBER_STATUS_MASK DDS_DATA_ON_READERS_STATUS
#define DDS_PUBLISHER_STATUS_MASK 0
#define DDS_PARTICIPANT_STATUS_MASK 0

extern const uint32_t dds_status_masks [DDS_ENTITY_NUM];

int dds_status_check (dds_entity_kind_t kind, uint32_t mask);

#if defined (__cplusplus)
}
#endif
#endif
