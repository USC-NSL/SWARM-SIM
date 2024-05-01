build_lib(
  LIBNAME single-flow-application
  SOURCE_FILES
    model/single-flow-application.cc
    helper/single-flow-helper.cc
  HEADER_FILES
    model/single-flow-application.h
    helper/single-flow-helper.h
  LIBRARIES_TO_LINK ${libinternet} ${libapplications}
)
