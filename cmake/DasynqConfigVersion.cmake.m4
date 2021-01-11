`set(PACKAGE_VERSION 'dasynq_version`)
set(DASYNQ_MAJOR_VERSION 'dasynq_major`)


# Check for exact match is easy:

if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_EXACT true)
endif(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)


# To be compatible, major version must be same and whole version must be >=
# the requested version:

if (PACKAGE_FIND_VERSION_MAJOR EQUAL DASYNQ_MAJOR_VERSION)

  # Major matches: searched minor/patch must be <= actual version
  if (PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)
    set(PACKAGE_VERSION_COMPATIBLE true)
  endif (PACKAGE_FIND_VERSION VERSION_LESS_EQUAL PACKAGE_VERSION)

endif (PACKAGE_FIND_VERSION_MAJOR EQUAL DASYNQ_MAJOR_VERSION)'
