/****************************************************************

  Generated by Vortex Lite IDL to C Translator
  File name: RoundTrip.c
  Source: RoundTrip.idl
  Generated: Mon Mar 07 16:08:04 CET 2016
  Vortex Lite: V2.1.0

*****************************************************************/
#include "RoundTrip.h"


static const uint32_t RoundTripModule_DataType_ops [] =
{
  DDS_OP_ADR | DDS_OP_TYPE_SEQ | DDS_OP_SUBTYPE_1BY, offsetof (RoundTripModule_DataType, payload),
  DDS_OP_RTS
};

const dds_topic_descriptor_t RoundTripModule_DataType_desc =
{
  sizeof (RoundTripModule_DataType),
  sizeof (char *),
  DDS_TOPIC_NO_OPTIMIZE,
  0u,
  "RoundTripModule::DataType",
  NULL,
  2,
  RoundTripModule_DataType_ops,
  "<MetaData version=\"1.0.0\"><Module name=\"RoundTripModule\"><Struct name=\"DataType\"><Member name=\"payload\"><Sequence><Octet/></Sequence></Member></Struct></Module></MetaData>"
};
