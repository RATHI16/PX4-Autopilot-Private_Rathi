px4_add_library(arch_io_pins
	io_timer.c
	pwm_servo.c
)
target_link_libraries(arch_io_pins PRIVATE drivers_board)
