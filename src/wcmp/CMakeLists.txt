build_lib(
  LIBNAME wcmp
  SOURCE_FILES
    helper/wcmp-static-routing-helper.cc
    model/wcmp-weights.cc
    model/wcmp-hasher.cc
    model/wcmp-static-routing.cc
  HEADER_FILES
    helper/wcmp-static-routing-helper.h
    model/wcmp-weights.h
    model/wcmp-hasher.h
    model/wcmp-static-routing.h
  LIBRARIES_TO_LINK ${libinternet}
  TEST_SOURCES
    test/wcmp-static-routing-throughput-test.cc
)