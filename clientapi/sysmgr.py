# This module is intended to be compatible with both python2 and python3.
# It depends on the python2-future package to achieve this.

from __future__ import absolute_import, division, print_function
try:
	#from builtins import (bytes, str, open, super, range, zip, round, input, int, pow, object, filter, map)
	from builtins import (bytes, str, super, range, zip, int, object, filter, map)
except ImportError:
	print('sysmgr requires the python2-future package to run under python2')
	import sys
	sys.exit(0)
import socket
import threading
import numbers
import re
import sys

class SysmgrError(Exception):
	pass

class Sysmgr(object):
	def __init__(self, password='', host='127.0.0.1', port=4681):
		self.host     = host
		self.port     = port
		self.password = password

		self._lock           = threading.RLock()
		self._send_lock      = threading.RLock()
		self._recv_notify    = threading.Condition(self._lock)
		self._recv_engine_id = None
		self._sock_err       = None
		self._recv_buf       = ''
		self._partials       = {}
		self._response_queue = {}
		self._event_queue    = {}
		self._next_msgid     = 0

		self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		self._sock.connect((host, port))
		rpl, priv = self._req('AUTHENTICATE', password)[0]
		assert rpl == 'PRIVILEGE', 'AUTHENTICATE response not understood'
		if priv == 'NONE':
			raise SysmgrError('Invalid password: no access granted')

	def __del__(self):
		try:
			if self._sock is not None:
				self._sock.close()
		except:
			pass

	# Call without lock held!
	def _run_recv(self, skip_condition):
		with self._lock:
			while True:
				if skip_condition():
					return
				if self._sock_err is not None:
					raise RuntimeError('Earlier socket error received: {0}'.format(repr(self._sock_err)))
				if self._recv_engine_id is None:
					self._recv_engine_id = threading.current_thread().ident
					break # Go do that.  It will stop only when our message is received.
				else:
					self._recv_notify.wait()
					if self._sock_err is not None:
						raise RuntimeError('Earlier socket error received: {0}'.format(repr(self._sock_err)))
					# Can't return yet.  Might not have been our message.

		# From here on out, it's our repsonsibility to run the engine.
		# Make sure it doesn't terminate without notifying.
		try:
			if self._sock_err is not None:
				raise RuntimeError('Earlier socket error received: {0}'.format(repr(self._sock_err)))
			while True:
				while '\n' not in self._recv_buf:
					try:
						new_response = self._sock.recv(512)
					except Exception as e:
						with self._lock:
							self._sock.close()
							if self._sock_err is None:
								self._sock_err = e
								raise
							else:
								raise RuntimeError('Earlier socket error received: {0}'.format(repr(self._sock_err)))
					if not new_response:
						with self._lock:
							self._sock.close()
							if self._sock_err is None:
								self._sock_err = IOError('Connection closed')
								raise self._sock_err
							else:
								raise RuntimeError('Earlier socket error received: {0}'.format(repr(self._sock_err)))
					if not isinstance(new_response, type('')):
						new_response = new_response.decode('utf-8')
					self._recv_buf += new_response

				msg, self._recv_buf = self._recv_buf.split('\n',1)
				msg = self._parse_response(msg)
				if len(msg) > 1:
					self._partials.setdefault(msg[0],[]).append(msg[1:])
				else:
					fullmsg = self._partials.pop(msg[0],[])
					if (msg[0] % 2):
						# Completed reciept of a server-initiated message
						if len(fullmsg) != 1 or fullmsg[0][0] != 'EVENT':
							print('{0}: Ignoring unsupported server-initiated message: {1}'.format(repr(self), repr(msg)))
						with self._lock:
							self._event_queue.get(fullmsg[0][1],[]).append({
								'filter':    fullmsg[0][1],
								'crate':     fullmsg[0][2],
								'fru':       fullmsg[0][3],
								'card_name': fullmsg[0][4],
								'sensor':    fullmsg[0][5],
								'assertion': bool(fullmsg[0][6]),
								'offset':    fullmsg[0][7],
								})
							self._recv_notify.notify_all()
					else:
						with self._lock:
							if msg[0] not in self._response_queue:
								print('{0}: Ignoring unexpected response message: {1}'.format(repr(self), repr(fullmsg)))
							else:
								self._response_queue[msg[0]] = fullmsg
								self._recv_notify.notify_all()
					if skip_condition(): # received a message.  done now?
						break
		finally:
			with self._lock:
				self._recv_engine_id = None
				self._recv_notify.notify_all()

	@staticmethod
	def _format_request(msgid, request):
		req = ['{0}'.format(msgid)]
		for token in request:
			if isinstance(token, type('')):
				token = token.replace('"','""') # escape "s
				req.append('"{0}"'.format(token))
			else:
				req.append('{0}'.format(token))
		return ' '.join(req)+'\n'

	@staticmethod
	def _parse_response(response):
		tokens = ['']
		fresh = True
		quoted = False
		just_unquoted = False
		for c in response:
			if c == '"':
				if just_unquoted:
					tokens[-1] += '"' # False alarm, this is an escaped ".
					fresh = False
				just_unquoted = quoted
				quoted = not quoted
			elif c == ' ' and not quoted:
				try:
					tokens[-1] = int(tokens[-1], 0)
				except ValueError:
					pass
				tokens.append('')
				fresh = True
				just_unquoted = False
			else:
				tokens[-1] += c
				fresh = False
				just_unquoted = False
		try:
			tokens[-1] = int(tokens[-1], 0)
		except ValueError:
			pass
		if fresh:
			tokens.pop()
		return tokens

	def _send_raw_data(self, request):
		if not isinstance(request, type(b'')):
			request = request.encode('utf-8')
		with self._send_lock:
			while request:
				try:
					sent = self._sock.send(request)
				except Exception as e:
					if self._sock_err is not None:
						self._sock_err = e
					self._sock.close()
					raise self._sock_err
				if not sent:
					if self._sock_err is not None:
						self._sock_err = IOError('Unable to send to remote host')
					self._sock.close()
					raise self._sock_err
				request = request[sent:]

	# Call only without _lock held!
	def _req(self, *request):
		msg = None
		with self._send_lock:
			msgid = self._next_msgid
			self._next_msgid = (self._next_msgid+2) % (2**32-2)

		with self._lock:
			self._response_queue[msgid] = None # Record expectation of response
		request = self._format_request(msgid, request)
		self._send_raw_data(request)
		self._run_recv(lambda: self._response_queue[msgid] is not None)
		rsp = self._response_queue.pop(msgid)
		if rsp and rsp[0][0] == 'ERROR':
			raise SysmgrError(' '.join(rsp[0][1:]))
		return rsp

	@staticmethod
	def _rspzip(rsp, fields, typemap={}, adclass=dict, typedefault=lambda x: x):
		return list(map(lambda ent: adclass(**dict(map(lambda x: (x[0], typemap.get(x[0], typedefault)(x[1])), zip(fields, ent)))), rsp))

	@classmethod
	def fru_to_string(cls, fru):
		if isinstance(fru, type('')):
			fru = cls.string_to_fru(fru) # Validate it.
		'''Convert a FRU number to a human-readable string.'''
		if fru == 3 or fru == 4:
			return 'MCH{0:d}'.format(fru-2)
		elif fru >= 5 and fru <= 16:
			return 'AMC{0:02d}'.format(fru-4)
		elif fru == 30:
			return 'AMC13'
		elif fru == 29:
			return 'AMC14'
		elif fru >= 40 and fru <= 41:
			return 'CU{0:d}'.format(fru-39)
		elif fru >= 50 and fru <= 53:
			return 'PM{0:d}'.format(fru-49)
		elif fru > 255:
			raise ValueError('FRU numbers are 0-255')
		else:
			return 'FRU{0:d}'.format(fru)

	@classmethod
	def string_to_fru(cls, fru):
		if isinstance(fru, numbers.Number):
			fru = cls.fru_to_string(fru) # Validate it.
		m = re.match('^([A-Z]+)0*([1-9][0-9]*)$', fru)
		if m is None:
			raise ValueError('FRU string not correctly formatted')
		ftype = m.group(1)
		fnum = int(m.group(2))
		if ftype == 'MCH':
			return fnum+2
		elif ftype == 'CU':
			return fnum+39
		elif ftype == 'PM':
			return fnum+49
		elif ftype == 'FRU':
			return fnum
		elif ftype == 'AMC':
			if fnum == 14:
				return 29
			elif fnum == 13:
				return 30
			else:
				return fnum+4
		else:
			raise ValueError('FRU type string unknown')

	def list_crates(self):
		'''List crates.

		Returns a list of Crate objects.
		'''
		return self._rspzip(self._req('LIST_CRATES'), ('number','connected','mch', 'description'), {'connected':bool}, adclass=Crate)

	def list_cards(self, crate):
		'''List cards in a crate.

		Returns a list of Card objects.
		'''
		return self._rspzip(self._req('LIST_CARDS',crate), ('fru','mstate','name'), typemap={'fru':self.fru_to_string}, adclass=Card)

	def list_sensors(self, crate, fru):
		'''List sensors on a card.

		Returns a list of Sensor objects.
		'''
		fru = self.string_to_fru(fru)
		return self._rspzip(self._req('LIST_SENSORS',crate,fru), ('name','type','long_unit','short_unit'), adclass=Sensor)

	def sensor_read(self, crate, fru, sensor_name):
		'''Retrieve the current readings from a sensor.

		Returns a SensorReading object.
		'''
		fru = self.string_to_fru(fru)
		rsp = self._req('SENSOR_READ',crate,fru,sensor_name)
		rawresult = dict(rsp)
		return SensorReading(**{
			'raw': rawresult['RAW'],
			'threshold': float(rawresult['THRESHOLD']) if 'THRESHOLD' in rawresult else None,
			'eventmask': rawresult['EVENTMASK'],
			})

	def get_sdr(self, crate, fru, sensor=None):
		'''Return the raw FRU or sensor SDR as a list of bytes.
		
		Parsing it is up to you.
		'''
		fru = self.string_to_fru(fru)
		if sensor is None:
			sdr = self._req('GET_SDR',crate,fru)
		else:
			sdr = self._req('GET_SDR',crate,fru,sensor)
		# vvv Protocol does not prefix with 0x.  Have to redo parsing.
		return list(map(lambda x: int('0x'+('00'+'{0}'.format(x))[-2:], 16), sdr[0]))

	def get_sensor_thresholds(self, crate, fru, sensor):
		'''Retrieve the threshold values from a given sensor.

		Returns a SensorThresholds object.
		'''
		fields = ('lnc', 'lcr', 'lnr', 'unc', 'ucr', 'unr')
		fru = self.string_to_fru(fru)
		return self._rspzip(self._req('GET_THRESHOLDS',crate,fru,sensor), fields, adclass=SensorThresholds, typedefault=lambda x: None if x == '-' else x)[0]

	def set_sensor_thresholds(self, crate, fru, sensor, lnc=None, lcr=None, lnr=None, unc=None, ucr=None, unr=None):
		'''Set the threshold values for a given sensor.

		Note that these are in RAW form, not interpreted form.  You must handle
		the relevant conversions yourself using data acquired from the SDRs.

		lnc = Lower Non-Critical
		lcr = Lower Critical
		lnr = Lower Non-Recoverable
		unc = Upper Non-Critical
		ucr = Upper Critical
		unr = Upper Non-Recoverable

		Values not supplied are not set.
		'''
		fru = self.string_to_fru(fru)
		vals = map(lambda x: '-' if x is None else x, [lnc,lcr,lnr,unc,ucr,unr])
		self._req('SET_THRESHOLDS',crate,fru,sensor,*vals)

	def get_sensor_event_enables(self, crate, fru, sensor):
		'''Get the event enable state for a given sensor.

		Returns a SensorEventEnables object.
		'''
		fields = (
				'events_enabled',
				'scanning_enabled',
				'assertion_mask',
				'deassertion_mask',
				)
		fru = self.string_to_fru(fru)
		return self._rspzip(self._req('GET_EVENT_ENABLES',crate,fru,sensor), fields, adclass=SensorEventEnables)[0]

	def set_sensor_event_enables(self, crate, fru, sensor, events_enabled, scanning_enabled, assertion_mask, deassertion_mask):
		'''Set the event enable state for a given sensor.

		events_enabled:
			"All event messages enabled/disabled from this sensor."

		scanning_enabled:
			"Sensor scanning enabled/disabled."

		assertion_mask:
		deassertion_mask:
			The (de)assertion bitmask indicates what states have (de)assertion
			events enabled.

			For threshold sensors, the order of states is:

			Bit   Event
			---   -----
			 0    Lower noncritical going low
			 1    Lower noncritical going high
			 2    Lower critical going low
			 3    Lower critical going high
			 4    Lower nonrecoverable going low
			 5    Lower nonrecoverable going high
			 6    Upper noncritical going low
			 7    Upper noncritical going high
			 8    Upper critical going low
			 9    Upper critical going high
			 a    Upper nonrecoverable going low
			 b    Upper nonrecoverable going high
		'''
		fru = self.string_to_fru(fru)
		self._req('SET_EVENT_ENABLES',crate,fru,sensor,
				1 if events_enabled else 0,
				1 if scanning_enabled else 0,
				assertion_mask,
				deassertion_mask)


	def raw_card(self, crate, fru, cmd):
		'''Send a raw command to a given card.

		cmd =  [ NetFN, CMD, ParamList ]
		return [ CmplCode, ParamList ]
		'''
		fru = self.string_to_fru(fru)
		return self._req('RAW_CARD',crate,fru,*cmd)[0]

	def raw_direct(self, crate, target_chan, target_addr, cmd):
		'''Send a raw command to a given address on the Shelf IPMB.

		cmd =  [ NetFN, CMD, ParamList ]
		return [ CmplCode, ParamList ]
		'''
		return self._req('RAW_DIRECT', crate, target_chan, target_addr, *cmd)[0]

	def raw_forwarded(self, crate, bridge_chan, bridge_addr, target_chan, target_addr, cmd):
		'''Send a raw command to a given address, forwarded through another controller from the Shelf IPMB to another IPMB.

		cmd =  [ NetFN, CMD, ParamList ]
		return [ CmplCode, ParamList ]
		'''
		return self._req('RAW_FORWARDED', crate, bridge_chan, bridge_addr, target_chan, target_addr, *cmd)[0]


	def event_subscribe(self, crate=0xff, fru=0xff, card_name='', sensor_name='', assertion_mask=0x7fff, deassertion_mask=0x7fff):
		'''Subscribe to events matching the provided optional parameters.

		Returns an EventSubscription object.
		'''
		fru = self.string_to_fru(fru)
		rsp = self._req('SUBSCRIBE', crate, fru, card_name, sensor_name, assertion_mask, deassertion_mask)
		assert rsp[0][0] == 'FILTER'
		self._event_queue[rsp[0][1]] = []
		return EventSubscription(self, rsp[0][1],
				crate=crate, fru=self.fru_to_string(fru),
				card_name=card_name, sensor_name=sensor_name,
				assertion_mask=assertion_mask,
				deassertion_mask=deassertion_mask)

class _AttrDict(object):
	def __init__(self, **kwargs):
		self.__dict__.update(kwargs)
	def __repr__(self):
		def sortkey(x):
			if x[0].lower() == 'number':
				return (-3, x)
			elif x[0].lower() == 'fru':
				return (-2, x)
			elif x[0].lower() == 'name':
				return (-1, x)
			else:
				return (0, x)
		content = map(lambda x: '{0}={1}'.format(x[0], repr(x[1])), sorted(self.__dict__.items(), key=sortkey))
		content = ', '.join(content)
		return '{0}<{1}>'.format(type(self).__name__, content)

class Crate(_AttrDict):
	'''Information about a crate.

	.number      = Crate number
	.connected   = Boolean connection status
	.mch         = MCH type
	.description = Crate description
	'''

class Card(_AttrDict):
	'''Information about a FRU.

	.fru    = The card's FRU as a string
	.mstate = The card's M-State (255 = Unknown)
	.name   = The card's type name
	'''

class Sensor(_AttrDict):
	'''Information about a sensor.

	.name       = Sensor name
	.type       = Sensor reading type
	.long_unit  = Long unit name
	.short_unit = Short unit name
	'''

class SensorReading(_AttrDict):
	'''A sensor reading.

	.raw       = Raw sensor reading
	.threshold = Converted sensor reading or None
	.eventmask = Sensor event mask
	'''

class SensorThresholds(_AttrDict):
	'''Information about a sensor's configured thresholds.

	These values are not interpreted.  Translating them is up to you.

	If the threshold was not readable, it will be None.

	.lnc = Lower Non-Critical
	.lcr = Lower Critical
	.lnr = Lower Non-Recoverable
	.unc = Upper Non-Critical
	.ucr = Upper Critical
	.unr = Upper Non-Recoverable
	'''

class SensorEventEnables(_AttrDict):
	'''Information about a sensor's currently enabled events.

	.events_enabled:
		"All event messages enabled/disabled from this sensor."

	.scanning_enabled:
		"Sensor scanning enabled/disabled."

	.assertion_mask:
	.deassertion_mask:
		The (de)assertion bitmask indicates what states have (de)assertion
		events enabled.

		For threshold sensors, the order of states is:

		Bit   Event
		---   -----
		 0    Lower noncritical going low
		 1    Lower noncritical going high
		 2    Lower critical going low
		 3    Lower critical going high
		 4    Lower nonrecoverable going low
		 5    Lower nonrecoverable going high
		 6    Upper noncritical going low
		 7    Upper noncritical going high
		 8    Upper critical going low
		 9    Upper critical going high
		 a    Upper nonrecoverable going low
		 b    Upper nonrecoverable going high
	'''

class EventSubscription(object):
	'''A subscription to a given set of events.'''

	def __init__(self, sysmgr, filterid, **kwargs):
		'''Private Constructor -- Do not use.'''
		self._sysmgr     = sysmgr
		self._filterid   = filterid
		self.__dict__.update(kwargs)

	def __setattr__(self, key, value):
		if not key.startswith('_'):
			raise AttributeError('{0} is not mutable'.format(key))
		else:
			return super(EventSubscription, self).__setattr__(key, value)

	def wait_for_event(self):
		'''Waits indefinitely for an IPMI event matching the subscription.

		Returns a single Event object.
		'''
		if self._filterid is None:
			raise RuntimeError('wait_for_event() called after unsubscribe()')
		self._sysmgr._run_recv(lambda: bool(self._sysmgr._event_queue[self._filterid]))
		event = self._sysmgr._event_queue[self._filterid].pop(0)
		del event['filter']
		return Event(**event)

	def poll_for_event(self):
		'''Checks but does not wait for an IPMI event matching the subscription.

		This will send a request to the server, and is not suitable for extremely rapid polling.

		Returns a single Event object, or None.
		'''
		if self._filterid is None:
			raise RuntimeError('poll_for_event() called after unsubscribe()')
		self._sysmgr.list_crates() # Simpler than nonblocking IO for the moment.
		# Any event messages pending receive are now in the event queue.
		with self._sysmgr._lock:
			if not self._sysmgr._event_queue[self._filterid]:
				return None
			else:
				event = self._sysmgr._event_queue[self._filterid].pop(0)
				del event['filter']
				return Event(**event)

	def unsubscribe(self):
		'''Cancels this event subscription.

		This object is useless after this call.  It cannot be reused.
		'''
		if self._filterid is not None:
			return self._sysmgr._req('UNSUBSCRIBE', self._filterid)
			self._filterid = None

	def __del__(self):
		try:
			self.unsubscribe()
		except:
			pass

class Event(_AttrDict):
	'''An IPMI event.

	.crate     = Crate number
	.fru       = FRU string
	.card_name = Card name
	.sensor    = Sensor name
	.assertion = (bool) Is this an assertion event?
	.offset    = Event offset
	'''

