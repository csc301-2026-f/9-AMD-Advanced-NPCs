#!/usr/bin/env bash
# Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
#
# Build gRPC v1.80.0 (protobuf 31.1) static libraries for macOS arm64 and
# install headers plus archives into Source/ThirdParty/gRPC/Mac.
#
# CMake options match the configuration that linked and launched the
# BackgroundNPCs editor on Apple Silicon with Unreal Engine 5.8:
# Apple Clang, arm64, static libraries, and the gRPC-bundled abseil,
# c-ares, protobuf, re2, BoringSSL, and zlib modules.
# protobuf_DISABLE_RTTI stays OFF to match that build. gRPC.Build.cs still
# defines GOOGLE_PROTOBUF_NO_RTTI=1 on every platform.
#
# The script only replaces Source/ThirdParty/gRPC/Mac/include and
# Source/ThirdParty/gRPC/Mac/lib. It does not read or write the Linux or
# Win64 third-party trees.
#
# Usage:
#   Resources/Build/mac_dependencies.sh
#
# Optional environment:
#   SCHOLA_GRPC_WORK_ROOT   clone and build directory
#                           (default: ${TMPDIR:-/tmp}/schola-grpc-v1.80.0)
#   branch                      gRPC tag (default: v1.80.0)
#   expected_protobuf_version   protobuf pin checked in version.json (default: 31.1)
#
# Unreal Engine 5.8 links Mac editor binaries for macOS 14.0
# (Engine/Config/Mac/Mac_SDK.json). Xcode 26's default deployment target is
# 15.0, which makes ld warn that every object was built for a newer OS.
# MACOSX_DEPLOYMENT_TARGET pins this build to the engine's target.

set -euo pipefail

branch="${branch:-v1.80.0}"
expected_protobuf_version="${expected_protobuf_version:-31.1}"
remote_origin="https://github.com/grpc/grpc.git"

if [[ "$(uname)" != "Darwin" ]]; then
    echo "This script builds macOS libraries and cannot run on $(uname)." >&2
    exit 1
fi
if [[ "$(uname -m)" != "arm64" ]]; then
    echo "This script targets Apple Silicon (arm64). This machine is $(uname -m)." >&2
    exit 1
fi

for tool in git cmake c++ cc perl; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "Required tool not found on PATH: ${tool}" >&2
        exit 1
    fi
done

if command -v ninja >/dev/null 2>&1; then
    cmake_generator=(-G Ninja)
else
    cmake_generator=(-G "Unix Makefiles")
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
mac_dir="${repo_root}/Source/ThirdParty/gRPC/Mac"
headers_dir="${mac_dir}/include"
libs_dir="${mac_dir}/lib"

case "${mac_dir}" in
    */Source/ThirdParty/gRPC/Mac) ;;
    *)
        echo "Refusing to install outside Source/ThirdParty/gRPC/Mac (resolved to ${mac_dir})." >&2
        exit 1
        ;;
esac

tmp_root="${TMPDIR:-/tmp}"
tmp_root="${tmp_root%/}"
work_root="${SCHOLA_GRPC_WORK_ROOT:-${tmp_root}/schola-grpc-v1.80.0}"
work_root="${work_root%/}"
grpc_root="${work_root}/src"
build_dir="${work_root}/build"
install_dir="${work_root}/install"

echo "repo_root=${repo_root}"
echo "work_root=${work_root}"
echo "mac_dir=${mac_dir}"

mkdir -p "${work_root}"

if [[ ! -d "${grpc_root}/.git" ]]; then
    echo "Cloning ${remote_origin} (${branch}) into ${grpc_root}"
    git clone --branch "${branch}" --depth 1 "${remote_origin}" "${grpc_root}"
else
    echo "Reusing ${grpc_root}"
    git -C "${grpc_root}" fetch --depth 1 origin "refs/tags/${branch}:refs/tags/${branch}"
    git -C "${grpc_root}" checkout -f "tags/${branch}"
fi

git -C "${grpc_root}" submodule update --init --recursive --depth 1 \
    third_party/abseil-cpp \
    third_party/cares/cares \
    third_party/re2 \
    third_party/protobuf \
    third_party/zlib \
    third_party/boringssl-with-bazel

protobuf_version_file="${grpc_root}/third_party/protobuf/version.json"
if ! grep -q "\"protoc_version\": \"${expected_protobuf_version}\"" "${protobuf_version_file}"; then
    echo "Expected protobuf ${expected_protobuf_version} from gRPC ${branch}, but ${protobuf_version_file} did not match." >&2
    exit 1
fi

# Test targets are disabled so a dependency rebuild cannot fail on tests.
# RE2_BUILD_TESTING defaults to ON; the shipped archives do not include its tests.
export MACOSX_DEPLOYMENT_TARGET=14.0
rm -rf "${install_dir}"
mkdir -p "${build_dir}" "${install_dir}"

cmake -S "${grpc_root}" -B "${build_dir}" \
    "${cmake_generator[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${install_dir}" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET}" \
    -DCMAKE_CXX_STANDARD=17 \
    -DCMAKE_CXX_STANDARD_REQUIRED=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DgRPC_BUILD_CSHARP_EXT=OFF \
    -DgRPC_ABSL_PROVIDER=module \
    -DgRPC_CARES_PROVIDER=module \
    -DgRPC_PROTOBUF_PROVIDER=module \
    -DgRPC_RE2_PROVIDER=module \
    -DgRPC_SSL_PROVIDER=module \
    -DgRPC_ZLIB_PROVIDER=module \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_BUILD_SHARED_LIBS=OFF \
    -Dprotobuf_DISABLE_RTTI=OFF \
    -DCARES_BUILD_TESTS=OFF \
    -DRE2_BUILD_TESTING=OFF

job_count="$(sysctl -n hw.ncpu)"
cmake --build "${build_dir}" --config Release --parallel "${job_count}"
cmake --install "${build_dir}" --config Release

if [[ ! -f "${install_dir}/include/grpcpp/grpcpp.h" ]]; then
    echo "Install prefix is missing grpcpp headers: ${install_dir}/include" >&2
    exit 1
fi

stage_dir="$(mktemp -d "${TMPDIR:-/tmp}/schola-grpc-stage.XXXXXX")"
cleanup_stage() {
    rm -rf "${stage_dir}"
}
trap cleanup_stage EXIT

mkdir -p "${stage_dir}/lib"
cp -R "${install_dir}/include" "${stage_dir}/include"
find "${install_dir}/lib" -maxdepth 1 -name '*.a' -exec cp -f {} "${stage_dir}/lib/" \;

lib_count="$(find "${stage_dir}/lib" -maxdepth 1 -name '*.a' | wc -l | tr -d ' ')"
header_count="$(find "${stage_dir}/include" -type f | wc -l | tr -d ' ')"
if [[ "${lib_count}" -lt 1 || "${header_count}" -lt 1 ]]; then
    echo "Refusing to replace ${mac_dir}: staged ${lib_count} libraries and ${header_count} headers." >&2
    exit 1
fi
for required in libgrpc.a libgrpc++.a libprotobuf.a; do
    if [[ ! -f "${stage_dir}/lib/${required}" ]]; then
        echo "Staged libraries are missing ${required}." >&2
        exit 1
    fi
done

minos="$(otool -l "${stage_dir}/lib/libz.a" | awk '/minos/{print $2; exit}')"
if [[ "${minos}" != "14.0" ]]; then
    echo "libz.a minos is '${minos}', expected 14.0. Refusing to replace ${mac_dir}." >&2
    exit 1
fi

# Replace the Mac tree only after the staged install is complete.
rm -rf "${headers_dir}" "${libs_dir}"
mkdir -p "${mac_dir}"
mv "${stage_dir}/include" "${headers_dir}"
mv "${stage_dir}/lib" "${libs_dir}"
trap - EXIT
rm -rf "${stage_dir}"

echo "Installed ${header_count} headers to ${headers_dir}"
echo "Installed ${lib_count} static libraries to ${libs_dir}"
