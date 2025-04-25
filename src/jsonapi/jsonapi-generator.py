#!/usr/bin/python3

# RetroShare JSON API generator
#
# Copyright (C) 2019  selankon <selankon@selankon.xyz>
# Copyright (C) 2021-2022  Gioacchino Mazzurco <gio@retroshare.cc>
# Copyright (C) 2021-2022  Asociación Civil Altermundi <info@altermundi.net>
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the
# Free Software Foundation, version 3.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License along
# with this program. If not, see <https://www.gnu.org/licenses/>
#
# SPDX-FileCopyrightText: Retroshare Team <contact@retroshare.cc>
# SPDX-License-Identifier: AGPL-3.0-only


# Original idea and implementation by G10h4ck (jsonapi-generator.cpp)
# Initial python reimplementation by Sehraf
#
# This python 3 script has superseded the original C++/Qt implementation this
# and is now used at build time in without depending on Qt.

import os
import sys

import xml.etree.ElementTree as ET
from string import Template


class MethodParam:
	_type = ''
	_name = ''
	_defval = ''
	_in = False
	_out = False
	_isMultiCallback = False
	_isSingleCallback = False


class TemplateOwn(Template):
	delimiter = '$%'
	pattern = '''
	\$%(?:
		(?P<escaped>\$\%) |   					# Escape sequence of two delimiters
		(?P<named>[_a-z][_a-z0-9]*)%\$      |   	# delimiter and a Python identifier
		{(?P<braced>[_a-z][_a-z0-9]*)}   |   	# delimiter and a braced identifier
		(?P<invalid>)              				# Other ill-formed delimiter exprs
	)
	'''


def getText(e):
	return "".join(e.itertext())


def processFile(file):
	try:
		dom1 = ET.parse(file).getroot()
	except FileNotFoundError:
		print('Can\'t open:', file)

	headerFileInfo = dom1[0].findall('location')[0].attrib['file']
	headerRelPath = os.path.dirname(headerFileInfo).split('/')[-1] + '/' + os.path.basename(headerFileInfo)

	for sectDef in dom1.findall('.//memberdef'):
		if sectDef.attrib['kind'] != 'variable' or sectDef.find('.//jsonapi') == None:
			continue

		instanceName = sectDef.find('name').text
		typeName = sectDef.find('type/ref').text

		typeFilePath = sectDef.find('type/ref').attrib['refid']

		try:
			dom2 = ET.parse(doxPrefix + typeFilePath + '.xml').getroot()
		except FileNotFoundError:
			print('Can\'t open:', doxPrefix + typeFilePath + '.xml')

		for member in dom2.findall('.//member'):
			refid = member.attrib['refid']
			methodName = member.find('name').text

			requiresAuth = True

			# As of 2022/03/24 doxygen behaves differently on MacOS and on Linux
			# on Linux we get
			# refid classRsBroadcastDiscovery_1a62f461807c36d60db35794cdb70bf032
			# file name classRsBroadcastDiscovery.xml
			# on MacOS we get
			# refid class_rs_broadcast_discovery_1a62f461807c36d60db35794cdb70bf032
			# file name class_rs_broadcast_discovery.xml
			# which have more the one _ therfore we need to use reverse single
			# split to build the file name from refid in a way that works on
			# both systems
			# On Windows haven't got reports about this so it probably behave
			# like on Linux, but now should work anyway.
			defFileName = refid.rsplit('_',1)[0] + '.xml'

			print( 'Looking for', typeName, methodName, 'into', defFileName,
				   "calculated from refid:", refid )

			try:
				defDoc = ET.parse(doxPrefix + defFileName).getroot()
			except FileNotFoundError:
				print( 'Can\'t open:', doxPrefix + defFileName,
					   "not found between:" )
				for mFileName in os.listdir(doxPrefix):
					print(mFileName)
				raise

			memberdef = None
			for tmpMBD in defDoc.findall('.//memberdef'):
				tmpId = tmpMBD.attrib['id']
				tmpKind = tmpMBD.attrib['kind']
				tmpJsonApiTagList = tmpMBD.findall('.//jsonapi')

				if len(tmpJsonApiTagList) != 0 and tmpId == refid and tmpKind == 'function':
					tmpJsonApiTag = tmpJsonApiTagList[0]

					tmpAccessValue = None
					if 'access' in tmpJsonApiTag.attrib:
						tmpAccessValue = tmpJsonApiTag.attrib['access']

					requiresAuth = 'unauthenticated' != tmpAccessValue;

					if 'manualwrapper' != tmpAccessValue:
						memberdef = tmpMBD

					break

			if memberdef == None:
				continue

			apiPath = '/' + instanceName + '/' + methodName

			retvalType = getText(memberdef.find('type'))
			# Apparently some xml declarations include new lines ('\n') and/or multiple spaces
			# Strip them using python magic
			retvalType = ' '.join(retvalType.split())

			paramsMap = {}
			orderedParamNames = []

			hasInput = False
			hasOutput = False
			hasSingleCallback = False
			hasMultiCallback = False
			callbackName = ''
			callbackParams = ''

			for tmpPE in memberdef.findall('param'):
				pType = getText(tmpPE.find('type'))
				is_likely_output_param = ('&' in pType and 'const' not in pType)

				# --- Robust Parameter Name Extraction (v3) ---
				pName = ''
				# 1. Try finding <parametername> inside <param>
				paramNameNode = tmpPE.find('parametername')
				if paramNameNode is not None and paramNameNode.text:
					pName = paramNameNode.text
				
				# 2. Fallbacks (prioritize defname for likely output params)
				if not pName:
					declNameNode = tmpPE.find('declname')
					defNameNode = tmpPE.find('defname')
					
					# Standard order: declname then defname
					if not is_likely_output_param: 
						if declNameNode is not None and declNameNode.text:
							pName = declNameNode.text
						elif defNameNode is not None and defNameNode.text:
							pName = defNameNode.text
					# Reversed order for likely output params: defname then declname
					else:
						if defNameNode is not None and defNameNode.text:
							pName = defNameNode.text
						elif declNameNode is not None and declNameNode.text:
							pName = declNameNode.text
				# --- End of Name Extraction ---

				# Skip parameter if no name could be determined
				if not pName:
					print(f"Warning: Could not determine parameter name for param type [{pType}] in {refid}. Skipping.")
					continue 

				# Create and populate the MethodParam object
				mp = MethodParam()
				mp._name = pName # Assign the determined name FIRST
				mp._type = pType # Assign the initial type
				
				tmpDefvalNode = tmpPE.find('defval')
				mp._defval = getText(tmpDefvalNode) if tmpDefvalNode is not None else ''
				mp._defval = ' '.join(mp._defval.split()) # Clean default value whitespace

				# --- Type Cleaning and Callback Detection ---
				cleaned_pType = pType 
				if cleaned_pType.startswith('const '): 
					cleaned_pType = cleaned_pType[6:]

				if cleaned_pType.startswith('std::function'):
					if cleaned_pType.endswith('&'): 
						cleaned_pType = cleaned_pType[:-1] 
					
					if pName.startswith('multiCallback'):
						mp._isMultiCallback = True
						hasMultiCallback = True
						callbackName = pName
						callbackParams = cleaned_pType
					elif pName.startswith('callback'):
						mp._isSingleCallback = True
						hasSingleCallback = True
						callbackName = pName 
						callbackParams = cleaned_pType 
					
					mp._type = cleaned_pType # Assign cleaned signature for callbacks

				else: # Not a callback, just clean the regular type
					# Don't remove '&' for regular types during cleaning here,
					# it might be needed later for serialization/declaration.
					# Just clean whitespace.
					# cleaned_pType = cleaned_pType.replace('&', '').replace(' ', '') 
					cleaned_pType = ' '.join(cleaned_pType.split())
					mp._type = cleaned_pType # Assign cleaned type
				# --- End of Type Cleaning ---

				# Add the parameter to the map and ordered list
				paramsMap[pName] = mp
				orderedParamNames.append(pName)

			for tmpPN in memberdef.findall('.//parametername'):
				# Add Try/Except block to handle cases where parameter name from docs doesn't match params from signature
				try:
					tmpParam = paramsMap[tmpPN.text]
				except KeyError:
					print(f"Warning: Parameter '{tmpPN.text}' found in Doxygen description for {refid}, but not in function signature parameters {list(paramsMap.keys())}. Skipping direction assignment.")
					continue # Skip to the next parametername tag

				tmpD = tmpPN.attrib['direction'] if 'direction' in tmpPN.attrib else ''

				if 'in' in tmpD:
					tmpParam._in = True
					hasInput = True
				if 'out' in tmpD:
					tmpParam._out = True
					hasOutput = True

			# Params sanity check
			for pmKey in paramsMap:
				pm = paramsMap[pmKey]
				if not (pm._isMultiCallback or pm._isSingleCallback or pm._in or pm._out):
					print( 'ERROR', 'Parameter:', pm._name, 'of:', apiPath,
							  'declared in:', headerRelPath,
							  'miss doxygen parameter direction attribute!',
							  defFileName )
					sys.exit(-1)

			functionCall = '\t\t'
			if retvalType != 'void':
				functionCall += retvalType + ' retval = '
				hasOutput = True
			functionCall += instanceName + '->' + methodName + '('
			functionCall += ', '.join(orderedParamNames) + ');\n'

			print(instanceName, apiPath, retvalType, typeName, methodName)
			for pn in orderedParamNames:
				mp = paramsMap[pn]
				print('\t', mp._type, mp._name, mp._in, mp._out)

			inputParamsDeserialization = ''
			if hasInput:
				inputParamsDeserialization += '\t\t{\n' 
				inputParamsDeserialization += '\t\t\tRsGenericSerializer::SerializeContext& ctx(cReq);\n' 
				inputParamsDeserialization += '\t\t\tRsGenericSerializer::SerializeJob j(RsGenericSerializer::FROM_JSON);\n';

			outputParamsSerialization = ''
			if hasOutput:
				outputParamsSerialization += '\t\t{\n'
				outputParamsSerialization += '\t\t\tRsGenericSerializer::SerializeContext& ctx(cAns);\n'
				outputParamsSerialization += '\t\t\tRsGenericSerializer::SerializeJob j(RsGenericSerializer::TO_JSON);\n';

			paramsDeclaration = ''
			for pn in orderedParamNames:
				mp = paramsMap[pn]
				# Determine the type for declaration (remove reference qualifier)
				declaration_type = mp._type
				is_reference = False
				if '&' in declaration_type:
					is_reference = True
					# Be careful not to remove '&' from pointer types like 'Type *&' if that occurs
					# Assuming simple references 'Type&' or 'const Type&' for now
					parts = declaration_type.split('&')
					if len(parts) == 2 and parts[1].strip() == '': # Check it's likely a simple reference
						declaration_type = parts[0].strip() 
					else:
						# Handle more complex cases or leave as is if unsure
						# For now, just log a warning if we encounter an unexpected reference type
						print(f"Warning: Encountered potentially complex reference type '{mp._type}' for parameter '{mp._name}'. Declaration might be incorrect.")
						declaration_type = declaration_type.replace('&', '').strip() # Fallback: simple removal

				paramsDeclaration += '\t\t' + declaration_type + ' ' + mp._name
				# Handle default values - skip default assignment for references 
				# as it's problematic and likely the source of some errors.
				# The function call will provide the value for output parameters.
				if mp._defval != '' and not is_reference: 
					paramsDeclaration += ' = ' + mp._defval
				paramsDeclaration += ';\n'
				if mp._in:
					inputParamsDeserialization += '\t\t\tRS_SERIAL_PROCESS('
					inputParamsDeserialization += mp._name + ');\n'
				if mp._out:
					outputParamsSerialization += '\t\t\tRS_SERIAL_PROCESS('
					outputParamsSerialization += mp._name + ');\n'

			if hasInput: 
				inputParamsDeserialization += '\t\t}\n'
			if retvalType != 'void': 
				outputParamsSerialization += '\t\t\tRS_SERIAL_PROCESS(retval);\n'
			if hasOutput:
				outputParamsSerialization += '\t\t}\n'

			captureVars = ''

			sessionEarlyClose = ''
			if hasSingleCallback:
				sessionEarlyClose = 'session->close();'

			sessionDelayedClose = ''
			if hasMultiCallback:
				sessionDelayedClose = """
		RsThread::async( [=]()
		{
			std::this_thread::sleep_for(
				std::chrono::seconds(maxWait+120) );
			auto lService = weakService.lock();
			if(!lService || lService->is_down()) return;
			lService->schedule( [=]()
			{
				auto session = weakSession.lock();
				if(session && session->is_open())
					session->close();
			} );
		} );
"""
				captureVars = 'this'

			callbackParamsSerialization = ''
			callbackParameterListString = '' # Store just the parameter list e.g. "const Type& name"

			if hasSingleCallback or hasMultiCallback:
				# Ensure callbackParams has the function signature before splitting
				if not callbackParams:
					print(f"ERROR: Callback parameter detected for {refid} but callbackParams string is empty!")
					sys.exit(-1)
				
				cbs = ''
				
				# Check if parentheses exist before splitting
				if '(' not in callbackParams or ')' not in callbackParams:
					print(f"ERROR: Could not parse callback parameters from signature '{callbackParams}' for {refid}")
					sys.exit(-1)

				try:
					tmp_params = callbackParams.split('(', 1)[1] # Split only once
					tmp_params = tmp_params.rsplit(')', 1)[0] # Split from right only once
					callbackParameterListString = tmp_params # Store the extracted parameter list
				except IndexError:
					print(f"ERROR: IndexError while parsing callback parameters from signature '{callbackParams}' for {refid}")
					sys.exit(-1)

				cbs += '\t\t\tRsGenericSerializer::SerializeContext ctx;\n'

				# Handle case with no parameters inside parentheses
				if tmp_params.strip():
					for cbPar in tmp_params.split(','):
						cbPar = cbPar.strip() # Clean whitespace
						if not cbPar: continue # Skip if empty after split/strip
						isConst = cbPar.startswith('const ')
						pSep = ' '
						isRef = '&' in cbPar
						if isRef: pSep = '&'
						# Find the last occurrence of the separator to correctly split type and name
						sepIndex = cbPar.rfind(pSep)
						if sepIndex == -1: # Handle cases like single type without separator (e.g., 'void')
							print(f"Warning: Could not properly parse callback parameter '{cbPar}' in {refid}. Assuming void/no name.")
							continue
						
						sepIndex += 1 # Move index past the separator
						cpt = cbPar[0:sepIndex].strip() # Type part
						cpn = cbPar[sepIndex:].strip() # Name part
						
						# Ensure name part is a valid identifier (basic check)
						if not cpn or not (cpn.isidentifier() or cpn.startswith('_')):
							print(f"Warning: Parsed callback parameter name '{cpn}' from '{cbPar}' in {refid} seems invalid. Skipping serialization.")
							continue
						
						# Adjust type part if it started with const
						if isConst: cpt = cpt[6:].strip()
						
						cbs += '\t\t\tRsTypeSerializer::serial_process('
						cbs += 'RsGenericSerializer::TO_JSON, ctx, '
						if isConst:
							cbs += 'const_cast<'
							cbs += cpt
							cbs += '>('
						cbs += cpn
						if isConst: cbs += ')'
						else:
							cbs += cpn
						cbs += ', \"'
						cbs += cpn
						cbs += '\" );\n'

				callbackParamsSerialization += cbs

			substitutionsMap = dict()
			substitutionsMap['paramsDeclaration'] = paramsDeclaration
			substitutionsMap['inputParamsDeserialization'] = inputParamsDeserialization
			substitutionsMap['outputParamsSerialization'] = outputParamsSerialization
			substitutionsMap['instanceName'] = instanceName
			substitutionsMap['functionCall'] = functionCall
			substitutionsMap['apiPath'] = apiPath
			substitutionsMap['sessionEarlyClose'] = sessionEarlyClose
			substitutionsMap['sessionDelayedClose'] = sessionDelayedClose
			substitutionsMap['captureVars'] = captureVars
			substitutionsMap['callbackName'] = callbackName
			substitutionsMap['callbackParams'] = callbackParameterListString
			substitutionsMap['callbackParamsSerialization'] = callbackParamsSerialization
			substitutionsMap['requiresAuth'] = 'true' if requiresAuth else 'false'

			# print(substitutionsMap)

			templFilePath = sourcePath
			if hasMultiCallback  or hasSingleCallback:
				templFilePath += '/async-method-wrapper-template.cpp.tmpl'
			else:
				templFilePath += '/method-wrapper-template.cpp.tmpl'

			templFile = open(templFilePath, 'r')
			wrapperDef = TemplateOwn(templFile.read())

			tmp = wrapperDef.substitute(substitutionsMap)
			wrappersDefFile.write(tmp)

			cppApiIncludesSet.add('#include "' + headerRelPath + '"\n')


if len(sys.argv) != 3:
	print('Usage:', sys.argv[0], 'SOURCE_PATH OUTPUT_PATH Got:', sys.argv[:])
	sys.exit(-1)

sourcePath = str(sys.argv[1])
outputPath = str(sys.argv[2])
doxPrefix = outputPath + '/xml/'

try:
	wrappersDefFile = open(outputPath + '/jsonapi-wrappers.inl', 'w')
except FileNotFoundError:
	print('Can\'t open:', outputPath + '/jsonapi-wrappers.inl')

try:
	cppApiIncludesFile = open(outputPath + '/jsonapi-includes.inl', 'w');
except FileNotFoundError:
	print('Can\'t open:', outputPath + '/jsonapi-includes.inl')

cppApiIncludesSet = set()

filesIterator = None
try:
	filesIterator = os.listdir(doxPrefix)
except FileNotFoundError:
	print("Doxygen xml output dir not found: ", doxPrefix)
	sys.exit(-1)

for file in filesIterator:
	if file.endswith("8h.xml"):
		processFile(os.path.join(doxPrefix, file))


for incl in cppApiIncludesSet:
	cppApiIncludesFile.write(incl)
