`set(DASYNQ_INCLUDE_DIRS 'include_dir`)

add_library(Dasynq::Dasynq INTERFACE IMPORTED)

set_target_properties(Dasynq::Dasynq PROPERTIES
   INTERFACE_INCLUDE_DIRECTORIES ${DASYNQ_INCLUDE_DIRS}
   INTERFACE_COMPILE_FEATURES cxx_std_11
   NO_SYSTEM_FROM_IMPORTED true
)

# Note that NO_SYSTEM_FROM_IMPORTED=true seems to have no effect.
# It apparently has to be used in the client project, and in that case
# it affects *all* dependencies for the target. Urgh.'
