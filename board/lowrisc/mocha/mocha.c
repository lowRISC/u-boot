// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2026 lowRISC Contributors.
 * Author: Alice Ziuziakowska <a.ziuziakowska@lowrisc.org>
 */

#if defined(CONFIG_MTD_NOR_FLASH)
int is_flash_available(void)
{
	return 0;
}
#endif

#if defined(CONFIG_BOARD_INIT)
int board_init(void)
{
	return 0;
}
#endif

#if defined(CONFIG_BOARD_LATE_INIT)
int board_late_init(void)
{
	return 0;
}
#endif
