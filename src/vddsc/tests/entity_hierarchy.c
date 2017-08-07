#include "dds.h"
#include "os/os.h"
#include <criterion/criterion.h>
#include <criterion/logging.h>
#include <criterion/theories.h>
#include "RoundTrip.h"

/* Add --verbose command line argument to get the cr_log_info traces (if there are any). */



/**************************************************************************************************
 *
 * Test fixtures
 *
 *************************************************************************************************/

static dds_entity_t g_keep        = 0;
static dds_entity_t g_participant = 0;
static dds_entity_t g_topic       = 0;
static dds_entity_t g_subscriber  = 0;
static dds_entity_t g_publisher   = 0;
static dds_entity_t g_reader_par  = 0;
static dds_entity_t g_reader_sub  = 0;
static dds_entity_t g_writer_par  = 0;
static dds_entity_t g_writer_pub  = 0;
static dds_entity_t g_readcond    = 0;
static dds_entity_t g_querycond   = 0;

/* Dummy query condition callback. */
static bool
accept_all(const void * sample)
{
    return true;
}

static char*
create_topic_name(const char *prefix, char *name, size_t size)
{
    /* Get semi random g_topic name. */
    os_procId pid = os_procIdSelf();
    uintmax_t tid = os_threadIdToInteger(os_threadIdSelf());
    snprintf(name, size, "%s_pid%"PRIprocId"_tid%"PRIuMAX"", prefix, pid, tid);
    return name;
}

static void
hierarchy_init(void)
{
    uint32_t mask = DDS_ANY_SAMPLE_STATE | DDS_ANY_VIEW_STATE | DDS_ANY_INSTANCE_STATE;
    char name[100];

    g_participant = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    cr_assert_gt(g_participant, 0, "Failed to create prerequisite g_participant");

    g_publisher = dds_create_publisher(g_participant, NULL, NULL);
    cr_assert_gt(g_publisher, 0, "Failed to create prerequisite g_publisher");

    g_subscriber = dds_create_subscriber(g_participant, NULL, NULL);
    cr_assert_gt(g_subscriber, 0, "Failed to create prerequisite g_subscriber");

    g_topic = dds_create_topic(g_participant, &RoundTripModule_DataType_desc, create_topic_name("vddsc_hierarchy_test", name, 100), NULL, NULL);
    cr_assert_gt(g_topic, 0, "Failed to create prerequisite g_topic");

    g_writer_par = dds_create_writer(g_participant, g_topic, NULL, NULL);
    cr_assert_gt(g_writer_par, 0, "Failed to create prerequisite g_writer_par");

    g_writer_pub = dds_create_writer(g_publisher, g_topic, NULL, NULL);
    cr_assert_gt(g_writer_pub, 0, "Failed to create prerequisite g_writer_pub");

    g_reader_par = dds_create_reader(g_participant, g_topic, NULL, NULL);
    cr_assert_gt(g_reader_par, 0, "Failed to create prerequisite g_reader_par");

    g_reader_sub = dds_create_reader(g_subscriber, g_topic, NULL, NULL);
    cr_assert_gt(g_reader_sub, 0, "Failed to create prerequisite g_reader_sub");

    g_readcond = dds_create_readcondition(g_reader_sub, mask);
    cr_assert_gt(g_readcond, 0, "Failed to create prerequisite g_readcond");

    g_querycond = dds_create_querycondition(g_reader_sub, mask, accept_all);
    cr_assert_gt(g_querycond, 0, "Failed to create prerequisite g_querycond");

    /* The deletion of the last participant will close down every thing. This
     * means that the API will react differently after that. Because the
     * testing we're doing here is quite generic, we'd like to not close down
     * everything when we delete our participant. For that, we create a second
     * participant, which will keep everything running.
     */
    g_keep = dds_create_participant(DDS_DOMAIN_DEFAULT, NULL, NULL);
    cr_assert_gt(g_keep, 0, "Failed to create prerequisite g_keep");
}

static void
hierarchy_fini(void)
{
    dds_delete(g_querycond);
    dds_delete(g_readcond);
    dds_delete(g_reader_sub);
    dds_delete(g_reader_par);
    dds_delete(g_writer_pub);
    dds_delete(g_writer_par);
    dds_delete(g_subscriber);
    dds_delete(g_publisher);
    dds_delete(g_topic);
    dds_delete(g_participant);
    dds_delete(g_keep);
}


#if 0
#else

/**************************************************************************************************
 *
 * These will check the recursive deletion.
 *
 *************************************************************************************************/
/*************************************************************************************************/
Test(vddsc_entity_delete, recursive, .init=hierarchy_init)
{
    dds_domainid_t id;
    dds_return_t ret;

    /* First be sure that 'dds_get_domainid' returns ok. */
    ret = dds_get_domainid(g_participant, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK);
    ret = dds_get_domainid(g_topic, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK);
    ret = dds_get_domainid(g_publisher, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK);
    ret = dds_get_domainid(g_subscriber, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK);
    ret = dds_get_domainid(g_writer_par, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK);
    ret = dds_get_domainid(g_writer_pub, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK);
    ret = dds_get_domainid(g_reader_par, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK);
    ret = dds_get_domainid(g_reader_sub, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK);
    ret = dds_get_domainid(g_readcond, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK);
    ret = dds_get_domainid(g_querycond, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK);

    /* By first deleting the topic, we force one of the readers
     * or writers to delete the topic. */
    ret = dds_delete(g_topic);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK);

    /* Deleting the top dog (participant) should delete all children. */
    ret = dds_delete(g_participant);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_OK, "returned %s", dds_err_str(ret));

    /* Check if all the entities are deleted now. */
    ret = dds_get_domainid(g_participant, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED);
    ret = dds_get_domainid(g_topic, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED);
    ret = dds_get_domainid(g_publisher, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED);
    ret = dds_get_domainid(g_subscriber, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED);
    ret = dds_get_domainid(g_writer_par, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED);
    ret = dds_get_domainid(g_writer_pub, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED);
    ret = dds_get_domainid(g_reader_par, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED);
    ret = dds_get_domainid(g_reader_sub, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED);
    ret = dds_get_domainid(g_readcond, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED);
    ret = dds_get_domainid(g_querycond, &id);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED);

    /* We don't do a fini. So... */
    dds_delete(g_keep);
}
/*************************************************************************************************/




/**************************************************************************************************
 *
 * These will check the dds_get_participant in various ways.
 *
 *************************************************************************************************/
/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_participant, valid_entities) = {
        DataPoints(dds_entity_t*, &g_readcond, &g_querycond, &g_reader_par, &g_reader_sub, &g_subscriber, &g_writer_par, &g_writer_pub, &g_publisher, &g_topic, &g_participant),
};
Theory((dds_entity_t *entity), vddsc_entity_get_participant, valid_entities, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t participant;
    participant = dds_get_participant(*entity);
    cr_assert_eq(participant, g_participant);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_participant, deleted_entities) = {
        DataPoints(dds_entity_t*, &g_readcond, &g_querycond, &g_reader_par, &g_reader_sub, &g_subscriber, &g_writer_par, &g_writer_pub, &g_publisher, &g_topic, &g_participant),
};
Theory((dds_entity_t *entity), vddsc_entity_get_participant, deleted_entities, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t participant;
    dds_delete(*entity);
    participant = dds_get_participant(*entity);
    cr_assert_eq(dds_err_nr(participant), DDS_RETCODE_ALREADY_DELETED, "returned %s", dds_err_str(participant));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_participant, invalid_entities) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t entity), vddsc_entity_get_participant, invalid_entities, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_entity_t participant;

    if (entity < 0) {
        /* Entering the API with an error should return the same error. */
        exp = entity;
    }

    participant = dds_get_participant(entity);
    cr_assert_eq(dds_err_nr(participant), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(participant), dds_err_nr(exp));
}
/*************************************************************************************************/





/**************************************************************************************************
 *
 * These will check the dds_get_parent in various ways.
 *
 *************************************************************************************************/
/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_parent, conditions) = {
        DataPoints(dds_entity_t*, &g_readcond, &g_querycond),
};
Theory((dds_entity_t *entity), vddsc_entity_get_parent, conditions, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t parent;
    parent = dds_get_parent(*entity);
    cr_assert_eq(parent, g_reader_sub);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_parent, reader, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t parent;
    parent = dds_get_parent(g_reader_sub);
    cr_assert_eq(parent, g_subscriber);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_parent, writer, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t parent;
    parent = dds_get_parent(g_writer_pub);
    cr_assert_eq(parent, g_publisher);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_parent, pubsubtop) = {
        DataPoints(dds_entity_t*, &g_publisher, &g_subscriber, &g_topic),
};
Theory((dds_entity_t *entity), vddsc_entity_get_parent, pubsubtop, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t parent;
    parent = dds_get_parent(*entity);
    cr_assert_eq(parent, g_participant);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_parent, participant, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t parent;
    parent = dds_get_parent(g_participant);
    cr_assert_eq(dds_err_nr(parent), DDS_RETCODE_ILLEGAL_OPERATION, "returned %d", dds_err_nr(parent));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_parent, deleted_entities) = {
        DataPoints(dds_entity_t*, &g_readcond, &g_querycond, &g_reader_par, &g_reader_sub, &g_subscriber, &g_writer_par, &g_writer_pub, &g_publisher, &g_topic, &g_participant),
};
Theory((dds_entity_t *entity), vddsc_entity_get_parent, deleted_entities, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t parent;
    dds_delete(*entity);
    parent = dds_get_parent(*entity);
    cr_assert_eq(dds_err_nr(parent), DDS_RETCODE_ALREADY_DELETED);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_parent, invalid_entities) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t entity), vddsc_entity_get_parent, invalid_entities, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_entity_t parent;

    if (entity < 0) {
        /* Entering the API with an error should return the same error. */
        exp = entity;
    }

    parent = dds_get_parent(entity);
    cr_assert_eq(dds_err_nr(parent), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(parent), dds_err_nr(exp));
}
/*************************************************************************************************/





/**************************************************************************************************
 *
 * These will check the dds_get_children in various ways.
 *
 *************************************************************************************************/
/*************************************************************************************************/
Test(vddsc_entity_get_children, null, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_return_t ret;
    ret = dds_get_children(g_participant, NULL, 0);
    cr_assert_eq(ret, 5);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_children, invalid_size, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_return_t ret;
    dds_entity_t child;
    ret = dds_get_children(g_participant, &child, INT32_MAX);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_BAD_PARAMETER);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_children, too_small, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_return_t ret;
    dds_entity_t children[2];
    ret = dds_get_children(g_participant, children, 2);
    cr_assert_eq(ret, 5);
    cr_assert((children[0] == g_publisher) || (children[0] == g_subscriber)  || (children[0] == g_topic) || (children[0] == g_writer_par)|| (children[0] == g_reader_par));
    cr_assert((children[1] == g_publisher) || (children[1] == g_subscriber)  || (children[1] == g_topic) || (children[0] == g_writer_par)|| (children[0] == g_reader_par));
    cr_assert_neq(children[0], children[1]);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_children, participant, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_return_t ret;
    dds_entity_t children[7];
    ret = dds_get_children(g_participant, children, 7);
    cr_assert_eq(ret, 5);
    /* Every child should be known. */
    cr_assert((children[0] == g_publisher) || (children[0] == g_subscriber)  || (children[0] == g_topic) || (children[0] == g_writer_par)|| (children[0] == g_reader_par));
    cr_assert((children[1] == g_publisher) || (children[1] == g_subscriber)  || (children[1] == g_topic) || (children[1] == g_writer_par)|| (children[1] == g_reader_par));
    cr_assert((children[2] == g_publisher) || (children[2] == g_subscriber)  || (children[2] == g_topic) || (children[2] == g_writer_par)|| (children[2] == g_reader_par));
    cr_assert((children[3] == g_publisher) || (children[3] == g_subscriber)  || (children[3] == g_topic) || (children[3] == g_writer_par)|| (children[3] == g_reader_par));
    cr_assert((children[4] == g_publisher) || (children[4] == g_subscriber)  || (children[4] == g_topic) || (children[4] == g_writer_par)|| (children[4] == g_reader_par));

    /* No child should be returned twice. */
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            cr_assert_neq(children[i], children[j]);
        }
    }
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_children, topic, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_return_t ret;
    dds_entity_t child;
    ret = dds_get_children(g_topic, &child, 1);
    cr_assert_eq(ret, 0);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_children, publisher, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_return_t ret;
    dds_entity_t child;
    ret = dds_get_children(g_publisher, &child, 1);
    cr_assert_eq(ret, 1);
    cr_assert_eq(child, g_writer_pub);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_children, subscriber, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_return_t ret;
    dds_entity_t children[2];
    ret = dds_get_children(g_subscriber, children, 2);
    cr_assert_eq(ret, 1);
    cr_assert_eq(children[0], g_reader_sub);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_children, writer, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_return_t ret;
    ret = dds_get_children(g_writer_pub, NULL, 0);
    cr_assert_eq(ret, 0);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_children, reader, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_return_t ret;
    dds_entity_t children[2];
    ret = dds_get_children(g_reader_sub, children, 2);
    cr_assert_eq(ret, 2);
    cr_assert((children[0] == g_readcond) || (children[0] == g_querycond));
    cr_assert((children[1] == g_readcond) || (children[1] == g_querycond));
    cr_assert_neq(children[0], children[1]);
}
/*************************************************************************************************/
/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_children, conditions) = {
        DataPoints(dds_entity_t*, &g_readcond, &g_querycond),
};
Theory((dds_entity_t *entity), vddsc_entity_get_children, conditions, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_return_t ret;
    dds_entity_t child;
    ret = dds_get_children(*entity, &child, 1);
    cr_assert_eq(ret, 0);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_children, deleted_entities) = {
        DataPoints(dds_entity_t*, &g_readcond, &g_querycond, &g_reader_par, &g_reader_sub, &g_subscriber, &g_writer_par, &g_writer_pub, &g_publisher, &g_topic, &g_participant),
};
Theory((dds_entity_t *entity), vddsc_entity_get_children, deleted_entities, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_return_t ret;
    dds_entity_t children[4];
    dds_delete(*entity);
    ret = dds_get_children(*entity, children, 4);
    cr_assert_eq(dds_err_nr(ret), DDS_RETCODE_ALREADY_DELETED);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_children, invalid_entities) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t entity), vddsc_entity_get_children, invalid_entities, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_entity_t children[4];
    dds_return_t ret;

    if (entity < 0) {
        /* Entering the API with an error should return the same error. */
        exp = entity;
    }

    ret = dds_get_children(entity, children, 4);
    cr_assert_eq(dds_err_nr(ret), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(ret), dds_err_nr(exp));
}
/*************************************************************************************************/





#if 0
/* TODO: CHAM-291 Switch tests on when dds_get_topic gets available. */
/**************************************************************************************************
 *
 * These will check the dds_get_topic in various ways.
 *
 *************************************************************************************************/
/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_topic, data_entities) = {
        DataPoints(dds_entity_t*, &g_readcond, &data_entities, &g_reader_sub, &g_writer_pub),
};
Theory((dds_entity_t *entity), vddsc_entity_get_topic, data_entities, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t topic;
    topic = dds_get_topic(*entity);
    cr_assert_eq(topic, g_topic);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_topic, deleted_entities) = {
        DataPoints(dds_entity_t*, &g_readcond, &g_querycond, &g_reader_sub, &g_writer_pub),
};
Theory((dds_entity_t *entity), vddsc_entity_get_topic, deleted_entities, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t topic;
    dds_delete(*entity);
    topic = dds_get_topic(*entity);
    cr_assert_eq(dds_err_nr(topic), DDS_RETCODE_ALREADY_DELETED);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_topic, invalid_entities) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t entity), vddsc_entity_get_topic, invalid_entities, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_entity_t topic;

    if (entity < 0) {
        /* Entering the API with an error should return the same error. */
        exp = entity;
    }

    topic = dds_get_topic(entity);
    cr_assert_eq(dds_err_nr(topic), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(topic), dds_err_nr(exp));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_topic, non_data_entities) = {
        DataPoints(dds_entity_t*, &g_subscriber, &g_publisher, &g_topic, &g_participant),
};
Theory((dds_entity_t *entity), vddsc_entity_get_topic, non_data_entities, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t topic;
    topic = dds_get_topic(*entity);
    cr_assert_eq(dds_err_nr(topic), DDS_RETCODE_ILLEGAL_OPERATION, "returned %d", dds_err_nr(topic));
}
/*************************************************************************************************/
#endif




/**************************************************************************************************
 *
 * These will check the dds_get_publisher in various ways.
 *
 *************************************************************************************************/
/*************************************************************************************************/
Test(vddsc_entity_get_publisher, writer, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t publisher;
    publisher = dds_get_publisher(g_writer_pub);
    cr_assert_eq(publisher, g_publisher);
}
/*************************************************************************************************/

/*************************************************************************************************/
Test(vddsc_entity_get_publisher, deleted_writer, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t publisher;
    dds_delete(g_writer_pub);
    publisher = dds_get_publisher(g_writer_pub);
    cr_assert_eq(dds_err_nr(publisher), DDS_RETCODE_ALREADY_DELETED);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_publisher, invalid_writers) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t entity), vddsc_entity_get_publisher, invalid_writers, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_entity_t publisher;

    if (entity < 0) {
        /* Entering the API with an error should return the same error. */
        exp = entity;
    }

    publisher = dds_get_publisher(entity);
    cr_assert_eq(dds_err_nr(publisher), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(publisher), dds_err_nr(exp));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_publisher, non_writers) = {
        DataPoints(dds_entity_t*, &g_publisher, &g_reader_sub, &g_subscriber, &g_topic, &g_participant),
};
Theory((dds_entity_t *cond), vddsc_entity_get_publisher, non_writers, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t publisher;
    publisher = dds_get_publisher(*cond);
    cr_assert_eq(dds_err_nr(publisher), DDS_RETCODE_ILLEGAL_OPERATION, "returned %d", dds_err_nr(publisher));
}
/*************************************************************************************************/




/**************************************************************************************************
 *
 * These will check the dds_get_subscriber in various ways.
 *
 *************************************************************************************************/
/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_subscriber, readers) = {
        DataPoints(dds_entity_t*, &g_readcond, &g_querycond, &g_reader_sub),
};
Theory((dds_entity_t *entity), vddsc_entity_get_subscriber, readers, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t subscriber;
    subscriber = dds_get_subscriber(*entity);
    cr_assert_eq(subscriber, g_subscriber);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_subscriber, deleted_readers) = {
        DataPoints(dds_entity_t*, &g_readcond, &g_querycond, &g_reader_sub),
};
Theory((dds_entity_t *entity), vddsc_entity_get_subscriber, deleted_readers, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t subscriber;
    dds_delete(*entity);
    subscriber = dds_get_subscriber(*entity);
    cr_assert_eq(dds_err_nr(subscriber), DDS_RETCODE_ALREADY_DELETED);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_subscriber, invalid_readers) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t entity), vddsc_entity_get_subscriber, invalid_readers, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_entity_t subscriber;

    if (entity < 0) {
        /* Entering the API with an error should return the same error. */
        exp = entity;
    }

    subscriber = dds_get_subscriber(entity);
    cr_assert_eq(dds_err_nr(subscriber), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(subscriber), dds_err_nr(exp));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_subscriber, non_readers) = {
        DataPoints(dds_entity_t*, &g_subscriber, &g_writer_pub, &g_publisher, &g_topic, &g_participant),
};
Theory((dds_entity_t *cond), vddsc_entity_get_subscriber, non_readers, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t subscriber;
    subscriber = dds_get_subscriber(*cond);
    cr_assert_eq(dds_err_nr(subscriber), DDS_RETCODE_ILLEGAL_OPERATION, "returned %d", dds_err_nr(subscriber));
}
/*************************************************************************************************/






/**************************************************************************************************
 *
 * These will check the dds_get_datareader in various ways.
 *
 *************************************************************************************************/
/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_datareader, conditions) = {
        DataPoints(dds_entity_t*, &g_readcond, &g_querycond),
};
Theory((dds_entity_t *cond), vddsc_entity_get_datareader, conditions, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t reader;
    reader = dds_get_datareader(*cond);
    cr_assert_eq(reader, g_reader_sub);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_datareader, deleted_conds) = {
        DataPoints(dds_entity_t*, &g_readcond, &g_querycond),
};
Theory((dds_entity_t *cond), vddsc_entity_get_datareader, deleted_conds, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t reader;
    dds_delete(*cond);
    reader = dds_get_datareader(*cond);
    cr_assert_eq(dds_err_nr(reader), DDS_RETCODE_ALREADY_DELETED);
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_datareader, invalid_conds) = {
        DataPoints(dds_entity_t, -2, -1, 0, 1, 100, INT_MAX, INT_MIN),
};
Theory((dds_entity_t cond), vddsc_entity_get_datareader, invalid_conds, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t exp = DDS_RETCODE_BAD_PARAMETER * -1;
    dds_entity_t reader;

    if (cond < 0) {
        /* Entering the API with an error should return the same error. */
        exp = cond;
    }

    reader = dds_get_datareader(cond);
    cr_assert_eq(dds_err_nr(reader), dds_err_nr(exp), "returned %d != expected %d", dds_err_nr(reader), dds_err_nr(exp));
}
/*************************************************************************************************/

/*************************************************************************************************/
TheoryDataPoints(vddsc_entity_get_datareader, non_conds) = {
        DataPoints(dds_entity_t*, &g_reader_sub, &g_subscriber, &g_writer_pub, &g_publisher, &g_topic, &g_participant),
};
Theory((dds_entity_t *cond), vddsc_entity_get_datareader, non_conds, .init=hierarchy_init, .fini=hierarchy_fini)
{
    dds_entity_t reader;
    reader = dds_get_datareader(*cond);
    cr_assert_eq(dds_err_nr(reader), DDS_RETCODE_ILLEGAL_OPERATION, "returned %d", dds_err_nr(reader));
}
/*************************************************************************************************/


#endif
