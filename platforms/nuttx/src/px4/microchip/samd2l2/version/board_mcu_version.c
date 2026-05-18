/****************************************************************************
 * SAMD21 board_mcu_version — minimal implementation.
 * SAMD21 does not have a CHIPID peripheral like SAMV7.
 ****************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/defines.h>

int board_mcu_version(char *rev, const char **revstr, const char **errata)
{
	*rev    = 'A';
	*revstr = "SAMD21J18A";

	if (errata) {
		*errata = NULL;
	}

	return 0;
}
