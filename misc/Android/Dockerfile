## To prepare an image suitable as base for Gitlab CI use
## image name must match gitlab repository name, you can play just with the tag
## the part after :
# export ANDROID_MIN_API_LEVEL=16
# export CI_IMAGE_NAME="registry.gitlab.com/retroshare/retroshare:android_aar_base_${ANDROID_MIN_API_LEVEL}"
# docker build --squash --tag "${CI_IMAGE_NAME}" --build-arg ANDROID_MIN_API_LEVEL=${ANDROID_MIN_API_LEVEL} --build-arg LIBRETROSHARE_SOURCE_VERSION="$(git describe --always)" --file misc/Android/Dockerfile .

## To fit GitlabCI shared runners as they are limited to 25GB disk size
## use --squash
## and set
## GRADLE_BUILD_TASK to bundleDebugAar or bundleReleaseAar
## JNI_NATIVE_LIBS_ARCHS to arm64-v8a or armeabi-v7a

## To push it to gitlab CI registry you need first to login and the to push
# docker login registry.gitlab.com
# docker push ${CI_IMAGE_NAME}

## To esly extract artifacts and other files you ar einterested into from the
## image you can use a command similar to:
# docker cp $(docker create --rm ${CI_IMAGE_NAME}):/libretroshare-aar/* $HOME/
## see https://stackoverflow.com/a/59055906

FROM ubuntu:21.10

ENV DEBIAN_FRONTEND=noninteractive
ENV APT_UNAT="--assume-yes --quiet"

RUN apt-get update $APT_UNAT && apt-get upgrade --show-upgraded $APT_UNAT && \
	apt-get clean $APT_UNAT
RUN apt-get install --no-install-recommends $APT_UNAT \
	bash build-essential bzip2 cmake curl chrpath doxygen \
	git python python3 tclsh unzip wget zip

# Dependencies to create Android pkg
RUN apt-get install --no-install-recommends $APT_UNAT \
	openjdk-11-jre openjdk-11-jdk openjdk-11-jdk-headless

ENV ANDROID_SDK_PATH="/opt/android-sdk"
ENV ANDROID_HOME="$ANDROID_SDK_PATH"
ENV ANDROID_SDK_ROOT="$ANDROID_SDK_PATH"
ENV JAVA_HOME="/usr/lib/jvm/java-11-openjdk-amd64/"

ARG SOURCE_PATH=./
COPY $SOURCE_PATH /libretroshare

WORKDIR /libretroshare-build/
RUN /libretroshare/misc/Android/prepare-toolchain-clang.sh install_android_sdk

WORKDIR /libretroshare/
ARG ANDROID_MIN_API_LEVEL=16
ARG LIBRETROSHARE_SOURCE_VERSION="Dockerfile_unset"
ARG GRADLE_BUILD_TASK="build"
ARG JNI_NATIVE_LIBS_ARCHS="arm64-v8a armeabi-v7a"
ARG NATIVE_TOOLCHAINS_DIR="/libretroshare-native-toolchains/"
RUN ./gradlew $GRADLE_BUILD_TASK \
	-Dorg.gradle.project.buildDir=/libretroshare-build/ \
	-PANDROID_MIN_API_LEVEL=$ANDROID_MIN_API_LEVEL \
	-PNATIVE_TOOLCHAINS_DIR="$NATIVE_TOOLCHAINS_DIR" \
	-PJNI_NATIVE_LIBS_ARCHS="$JNI_NATIVE_LIBS_ARCHS" \
	-PLIBRETROSHARE_SOURCE_VERSION="$LIBRETROSHARE_SOURCE_VERSION"

WORKDIR /

## Save generated AAR to make life easier to newcomers that might want to use
## this just to build the libretroshare AAR
RUN mv /libretroshare-build/outputs/aar/ /libretroshare-aar/

## Cleanup all the rest now unneded stuff so squashing is more effective
RUN rm -rf /libretroshare-build/

## Clean reamining cruft inside native_toolchains
RUN rm -rf $NATIVE_TOOLCHAINS_DIR/*-build

## Clean apt cache
RUN apt-get clean && rm -rf /var/lib/apt/lists/*

## No need to keep sources inside the image
RUN rm -rf /libretroshare/
