px4_add_library(arch_hrt
	hrt.c
)
target_compile_options(arch_hrt PRIVATE ${MAX_CUSTOM_OPT_LEVEL})
