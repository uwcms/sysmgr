#!/usr/bin/env python
from __future__ import print_function
import glob
import re
import sys
import tornado.template
import pprint

cards = {}
for card in glob.glob('cards/*.cpp'):
	cardinfo = {
			'name': re.sub('^cards/(.*)\.cpp$','\\1', card),
			'file': card,
			'depends': set(),
			'config': '',
			}
	cards[cardinfo['name']] = cardinfo
	with open(card,'r') as fd:
		in_config = False
		for line in fd:
			if in_config:
				if line.strip().startswith('*/'):
					in_config = False
				else:
					cardinfo['config'] += line
			m = re.match('^/\*\* configure: depends:\s*([^*]+)\s*\*/$', line.strip())
			if m is not None:
				cardinfo['depends'].update(set(m.group(1).strip().split()))
			m = re.match('^/\*\* configure: sysmgr.conf.example\s*$', line.strip())
			if m is not None:
				in_config = True

def gen_card_dag(cards):
	ordered = []
	queue = list(cards.keys())
	while queue:
		current = queue.pop()
		if current in ordered:
			continue
		ordered.insert(0, current)
		queue.extend(cards[current]['depends'])
	return list(map(lambda x: cards[x], ordered))

sorted_cards = gen_card_dag(cards) # generate a flattened dependency dag, with dependencies first

with open('sysmgr.example.conf','w') as fd:
	fd.write(
		tornado.template.Template(
			open('sysmgr.example.tmpl.conf','r').read(),
			autoescape=None
			).generate(**globals())
		)

commands = []
for command in glob.glob('commands/*.h'):
	with open(command,'r') as fd:
		for line in fd:
			m = re.match('^\s*class\s+Command_(\S+)(?:\s*:\s+([^{]+))?(?:{|$)', line)
			if m is not None:
				commands.append((m.group(1), command))

with open('commandindex.inc','w') as fd:
	for command in commands:
		fd.write('REGISTER_COMMAND({0});\n'.format(command[0]))

with open('commandindex.h','w') as fd:
	for command in commands:
		fd.write('#include "{1}"\n'.format(*command))

with open('sysmgr.spec','w') as fd:
	fd.write(
			tornado.template.Template(
				open("sysmgr.tmpl.spec","r").read(),
				autoescape=None
				).generate(**globals())
			)
