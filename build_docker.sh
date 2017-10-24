set -ex

SHA=$(git rev-parse HEAD)

DOCKER_TAG="${SHA}"

REPO=${REPO:-gcr.io}

TAG=${TAG:-$DOCKER_TAG}

PROJECT=${PROJECT:-soloio}

# DEBUG_IMAGE_NAME=${DEBUG_IMAGE_NAME:-${REPO}/${PROJECT}/envoy-debug:${TAG}}
DEBUG_IMAGE_NAME=${DEBUG_IMAGE_NAME:-${PROJECT}/envoy-debug:${TAG}}


BAZEL_TARGET_DIR="bazel-bin/"
rm -f ${BAZEL_TARGET_DIR}/envoy
bazel build -c dbg //:envoy
cp ../proxy/tools/deb/istio-iptables.sh ${BAZEL_TARGET_DIR}
cp ../proxy/tools/deb/istio-start.sh ${BAZEL_TARGET_DIR}
cp ../proxy/tools/deb/envoy.json ${BAZEL_TARGET_DIR}
cp ../proxy/docker/proxy-* ${BAZEL_TARGET_DIR}
cp ../proxy/docker/Dockerfile.debug ${BAZEL_TARGET_DIR}
docker build -f ${BAZEL_TARGET_DIR}/Dockerfile.debug -t "${DEBUG_IMAGE_NAME}" ${BAZEL_TARGET_DIR}
