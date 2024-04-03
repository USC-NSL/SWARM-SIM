build_lib(
  LIBNAME wcmp
  SOURCE_FILES
    model/wcmp-weights.cc
    model/wcmp-hasher.cc
    model/wcmp-static-routing.cc
  HEADER_FILES
    model/wcmp-weights.h
    model/wcmp-hasher.h
    model/wcmp-static-routing.h
  LIBRARIES_TO_LINK ${libinternet}
)