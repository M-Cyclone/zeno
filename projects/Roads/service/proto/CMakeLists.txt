FILE(GLOB_RECURSE PROTO_FILES *.proto)

INCLUDE(../../../RPC/cmake/ProtoHelper.cmake)

ADD_PROTO_FILES(${PROTO_FILES})
