#!/bin/bash

# Script to prepare libretroshare Android testing and publishing Docker images
#
# Copyright (C) 2016-2022  Gioacchino Mazzurco <gio@eigenlab.org>
# Copyright (C) 2020-2022  Asociación Civil Altermundi <info@altermundi.net>
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

set -e

## Define default value for variable, take two arguments, $1 variable name,
## $2 default variable value, if the variable is not already define define it
## with default value.
function define_default_value()
{
	VAR_NAME="${1}"
	DEFAULT_VALUE="${2}"

	[ -z "${!VAR_NAME}" ] && export ${VAR_NAME}="${DEFAULT_VALUE}" || true
}

define_default_value LIBRETROSHARE_SOURCE_VERSION "$(git describe --always)"
define_default_value ANDROID_MIN_API_LEVELS "16 21 24"
define_default_value JNI_NATIVE_LIBS_ARCHS "arm64-v8a armeabi-v7a"
define_default_value GRADLE_FULL_TASK "build" # set to "none" to disable
define_default_value GRADLE_SPLIT_TASKS "bundleDebugAar bundleReleaseAar"


function buildDetachPush()
{
	IMAGE_NAME="$1"

	docker build --squash --tag "${IMAGE_NAME}" \
		--build-arg ANDROID_MIN_API_LEVEL=${ANDROID_MIN_API_LEVEL} \
		--build-arg LIBRETROSHARE_SOURCE_VERSION="$LIBRETROSHARE_SOURCE_VERSION" \
		--build-arg GRADLE_BUILD_TASK="$GRADLE_BUILD_TASK" \
		--build-arg JNI_NATIVE_LIBS_ARCHS="$CURR_JNI_NATIVE_LIBS_ARCHS" \
		--file misc/Android/Dockerfile .

	# Start pushing in parallel with other build
	((sleep 1m ; docker push "$IMAGE_NAME")&)
}

for mApiLevel in $(shuf --echo $ANDROID_MIN_API_LEVELS) ; do
	ANDROID_MIN_API_LEVEL=$mApiLevel
	CURR_JNI_NATIVE_LIBS_ARCHS="$JNI_NATIVE_LIBS_ARCHS"
	[ "$mApiLevel" -gt "16" ] || CURR_JNI_NATIVE_LIBS_ARCHS="armeabi-v7a"

	[ "$GRADLE_FULL_TASK" == "none" ] ||
	{
		GRADLE_BUILD_TASK="$GRADLE_FULL_TASK"
		CI_IMAGE_NAME="registry.gitlab.com/retroshare/retroshare:android_aar_base_${ANDROID_MIN_API_LEVEL}"

		buildDetachPush "$CI_IMAGE_NAME"
	}

	[ "$GRADLE_SPLIT_TASKS" == "none" ] ||
	for mArch in $(shuf --echo $CURR_JNI_NATIVE_LIBS_ARCHS) ; do
		for mTask in $(shuf --echo $GRADLE_SPLIT_TASKS); do
			GRADLE_BUILD_TASK=$mTask
			CURR_JNI_NATIVE_LIBS_ARCHS=$mArch
			CI_IMAGE_NAME="registry.gitlab.com/retroshare/retroshare:android_aar_base_${ANDROID_MIN_API_LEVEL}_${CURR_JNI_NATIVE_LIBS_ARCHS}_${GRADLE_BUILD_TASK}"

			buildDetachPush "$CI_IMAGE_NAME"
		done
	done
done
