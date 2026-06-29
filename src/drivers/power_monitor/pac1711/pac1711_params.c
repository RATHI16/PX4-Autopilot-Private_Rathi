/**
 * PAC1711 Max Current
 *
 * Maximum measurable current in Amps.
 * Default 150A for EV79R88A power module with 0.3mOhm shunt.
 *
 * @group Power Monitor
 * @decimal 1
 * @unit A
 */
PARAM_DEFINE_FLOAT(PAC1711_CURRENT, 150.0f);

/**
 * PAC1711 Shunt Resistance
 *
 * Shunt resistor value in Ohms.
 * Default 0.0003 (0.3 mOhm) for EV79R88A power module.
 *
 * @group Power Monitor
 * @decimal 6
 * @unit Ohm
 */
PARAM_DEFINE_FLOAT(PAC1711_SHUNT, 0.0003f);
