`set(PACKAGE_VERSION 'dasynq_version`)
set(DASYNQ_MAJOR_VERSION 'dasynq_major`)

#######
### Below here should not require modification.
#######

# Check for exact match is easy:

if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_EXACT true)
endif(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)


# To be compatible, major version must be same and whole version must be >=
# the requested version:

if (PACKAGE_FIND_VERSION_MAJOR EQUAL DASYNQ_MAJOR_VERSION)

  # Major matches: minor/patch must be >=
  if (PACKAGE_FIND_VERSION VERSION_GREATER_EQUAL PACKAGE_VERSION)
    set(PACKAGE_VERSION_COMPATIBLE true)
  endif (PACKAGE_FIND_VERSION VERSION_GREATER_EQUAL PACKAGE_VERSION)

endif (PACKAGE_FIND_VERSION_MAJOR EQUAL DASYNQ_MAJOR_VERSION)'
