############################################################################
#
#   Copyright (c) 2024 PX4 Development Team. All rights reserved.
#
# SAMD21J18A PX4IO co-processor board support library.
#
############################################################################

add_library(drivers_board
	init.c
	timer_config.cpp
)

target_link_libraries(drivers_board
	PRIVATE
		nuttx_arch
)
