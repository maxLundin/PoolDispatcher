
add_library(pool_lib STATIC PoolDispatcher.h TaskQueue.h QueueDistribution.h)
set_target_properties(pool_lib PROPERTIES LINKER_LANGUAGE CXX)