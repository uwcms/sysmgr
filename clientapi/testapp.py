# This module is intended to be compatible with both python2 and python3.
# It depends on the python2-future package to achieve this.

from __future__ import absolute_import, division, print_function
#from builtins import (bytes, str, open, super, range, zip, round, input, int, pow, object, filter, map)
from builtins import (bytes, str, super, range, zip, int, object, filter, map)
import time
import sys
import sysmgr
from pprint import pprint

TEST_CRATE        = 1
TEST_FRU          = 'AMC04'
TEST_WRITE        = False
TEST_WRITE_SENSOR = '+12V'

s = sysmgr.Sysmgr()

print('Assertions')
for f in 'AMC01 AMC02 AMC03 AMC10 AMC12 CU1 CU2 PM1 PM2 MCH1 MCH2 AMC13 AMC14'.split(' '):
	assert f == s.fru_to_string(f), 'FRU string {0} changes in double-conversion'.format(f)
print()

print('list_crates():')
pprint(s.list_crates())
print()

print('list_cards({0}):'.format(TEST_CRATE))
pprint(s.list_cards(TEST_CRATE))
print()

print('list_sensors({0}, \'{1}\'):'.format(TEST_CRATE, TEST_FRU))
pprint(s.list_sensors(TEST_CRATE, TEST_FRU))
print()

print('sensor_read({0}, \'{1}\', \'Bottom Temp\'):'.format(TEST_CRATE, TEST_FRU))
pprint(s.sensor_read(TEST_CRATE, TEST_FRU, 'Bottom Temp'))
print()

print('get_sdr({0}, \'{1}\'):'.format(TEST_CRATE, TEST_FRU))
print(s.get_sdr(TEST_CRATE, TEST_FRU))
print()

print('get_sdr({0}, \'{1}\', \'Bottom Temp\'):'.format(TEST_CRATE, TEST_FRU))
print(s.get_sdr(TEST_CRATE, TEST_FRU, 'Bottom Temp'))
print()

print('get_sensor_thresholds({0}, \'{1}\', \'{2}\'):'.format(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR))
thresh = s.get_sensor_thresholds(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR)
pprint(thresh)
print()

print('set_sensor_thresholds({0}, \'{1}\', \'{2}\', ...):'.format(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR))
if TEST_WRITE:
	s.set_sensor_thresholds(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR, lnr=1, lcr=2, lnc=3, unr=0xf3, ucr=0xf2, unc=0xf1)
	t = s.get_sensor_thresholds(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR)
	pprint(t)
	assert t.lnr == 1,    'Set failed for lnr'
	assert t.lcr == 2,    'Set failed for lcr'
	assert t.lnc == 3,    'Set failed for lnc'
	assert t.unc == 0xf1, 'Set failed for unc'
	assert t.ucr == 0xf2, 'Set failed for ucr'
	assert t.unr == 0xf3, 'Set failed for unr'
	s.set_sensor_thresholds(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR,
			lnr=thresh.lnr,
			lcr=thresh.lcr,
			lnc=thresh.lnc,
			unc=thresh.unc,
			ucr=thresh.ucr,
			unr=thresh.unr)
	t = s.get_sensor_thresholds(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR)
	pprint(t)
	assert t.lnr == thresh.lnr, 'Restore failed for lnr'
	assert t.lcr == thresh.lcr, 'Restore failed for lcr'
	assert t.lnc == thresh.lnc, 'Restore failed for lnc'
	assert t.unc == thresh.unc, 'Restore failed for unc'
	assert t.ucr == thresh.ucr, 'Restore failed for ucr'
	assert t.unr == thresh.unr, 'Restore failed for unr'
	print('OK')
else:
	print('Skipped.')
print()

print('get_sensor_event_enables({0}, \'{1}\', \'{2}\'):'.format(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR))
evtena = s.get_sensor_event_enables(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR)
pprint(evtena)
print()

print('set_sensor_event_enables({0}, \'{1}\', \'{2}\', ...):'.format(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR))
if TEST_WRITE:
	s.set_sensor_event_enables(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR, events_enabled=True, scanning_enabled=True, assertion_mask=1, deassertion_mask=2)
	e = s.get_sensor_event_enables(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR)
	pprint(e)
	assert e.events_enabled == True, 'Set failed for events'
	assert e.scanning_enabled == True, 'Set failed for scanning'
	assert e.assertion_mask == 1, 'Set failed for assertion_mask'
	assert e.deassertion_mask == 2, 'Set failed for deassertion_mask'
	s.set_sensor_event_enables(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR,
			events_enabled=evtena.events_enabled,
			scanning_enabled=evtena.scanning_enabled,
			assertion_mask=evtena.assertion_mask,
			deassertion_mask=evtena.deassertion_mask)
	e = s.get_sensor_event_enables(TEST_CRATE, TEST_FRU, TEST_WRITE_SENSOR)
	pprint(e)
	assert e.events_enabled == evtena.events_enabled, 'Restore failed for events'
	assert e.scanning_enabled == evtena.scanning_enabled, 'Restore failed for scanning'
	assert e.assertion_mask == evtena.assertion_mask, 'Restore failed for assertion_mask'
	assert e.deassertion_mask == evtena.deassertion_mask, 'Restore failed for deassertion_mask'
	print('OK')
else:
	print('Skipped.')
print()

print('raw_card({0}, \'{1}\', [6,1]):'.format(TEST_CRATE, TEST_FRU))
print('Get Device ID: ' + repr(s.raw_card(TEST_CRATE, TEST_FRU, [6,1])))
print()

TEST_CARD_ADDR = 0x70 + (2 * (s.string_to_fru(TEST_FRU)-4))
print('raw_forwarded({0}, 0, 0x82, 7, 0x{1:02x}, [6,1]):'.format(TEST_CRATE, TEST_CARD_ADDR))
print('Get Device ID: ' + repr(s.raw_forwarded(TEST_CRATE, 0, 0x82, 7, TEST_CARD_ADDR, [6,1])))
print()

print('raw_direct({0}, 0, 0x20, [6,1]):'.format(TEST_CRATE))
print('Get Device ID: ' + repr(s.raw_direct(TEST_CRATE, 0, 0x20, [6,1])))
print()

sub = s.event_subscribe()
print('Waiting for an event:')
pprint(sub.wait_for_event())
print()

print('Sleeping 5 seconds.')
time.sleep(5)

print('Polling for an event:')
pprint(sub.poll_for_event())
print()
