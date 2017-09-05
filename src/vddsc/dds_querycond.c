#include <assert.h>
#include "kernel/dds_types.h"
#include "kernel/dds_entity.h"
#include "kernel/dds_reader.h"
#include "kernel/dds_topic.h"
#include "kernel/dds_querycond.h"
#include "kernel/dds_readcond.h"
#include "ddsi/ddsi_ser.h"
#include "kernel/dds_report.h"

_Pre_satisfies_((reader & DDS_ENTITY_KIND_MASK) == DDS_KIND_READER)
DDS_EXPORT dds_entity_t
dds_create_querycondition(
        _In_ dds_entity_t reader,
        _In_ uint32_t mask,
        _In_ dds_querycondition_filter_fn filter)
{
    dds_entity_t topic;
    dds_entity_t hdl;
    dds_retcode_t rc;
    dds_reader *r;
    dds_topic  *t;

    DDS_REPORT_STACK();

    rc = dds_reader_lock(reader, &r);
    if (rc == DDS_RETCODE_OK) {
        dds_readcond *cond = dds_create_readcond(r, DDS_KIND_COND_QUERY, mask);
        assert(cond);
        hdl = cond->m_entity.m_hdl;
        cond->m_query.m_filter = filter;
        topic = r->m_topic->m_entity.m_hdl;
        dds_reader_unlock(r);
        rc = dds_topic_lock(topic, &t);
        if (rc == DDS_RETCODE_OK) {
            if (t->m_stopic->filter_sample == NULL) {
                t->m_stopic->filter_sample = dds_alloc(t->m_descriptor->m_size);
            }
            dds_topic_unlock(t);
        } else {
            (void)dds_delete(hdl);
            hdl = DDS_ERRNO(rc, "Error occurred on locking topic");
        }
    } else {
        hdl = DDS_ERRNO(rc, "Error occurred on locking reader");
    }
    DDS_REPORT_FLUSH(hdl != DDS_RETCODE_OK);
    return hdl;
}
