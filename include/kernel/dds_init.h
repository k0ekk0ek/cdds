#ifndef _DDS_INIT_H_
#define _DDS_INIT_H_

#include "kernel/dds_types.h"

#if defined (__cplusplus)
extern "C" {
#endif

dds_return_t
dds_init_impl(
        _In_ dds_domainid_t domain);

/**
 *Description : Initialization function, called from main. This operation
 *initializes all the required DDS resources,
 *handles configuration of domainid based on the input passed, parses and
 *configures middleware from a xml file and initializes required resources.
 *
 *Arguments :
 *-# Returns 0 on success or a non-zero error status
 **/
dds_return_t
dds_init(void);

/* Finalization function, called from main */

/**
 *Description : Finalization function, called from main. This operation
 *releases all the resources used by DDS.
 *
 *Arguments :
 *-# None
 **/
void
dds_fini(void);

#if defined (__cplusplus)
}
#endif
#endif
