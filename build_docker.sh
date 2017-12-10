set -ex

SHA=$(git rev-parse HEAD)

DOCKER_TAG="${SHA}"

REPO=${REPO:-docker.io}

TAG=${TAG:-$DOCKER_TAG}

PROJECT=${PROJECT:-soloio}

DEBUG_IMAGE_NAME=${DEBUG_IMAGE_NAME:-${REPO}/${PROJECT}/envoy-debug:${TAG}}
# DEBUG_IMAGE_NAME=${DEBUG_IMAGE_NAME:-${PROJECT}/envoy-debug:${TAG}}

# change this and workspace in the same time.
PROXY_SHA="6a9fe308431755e19358b76da38738c5af250b04"

BAZEL_TARGET_DIR="bazel-bin/"

PROXY_GIT=https://github.com/istio/proxy
git clone $PROXY_GIT proxy-tmp
cd proxy-tmp
git checkout $PROXY_SHA
cd ..

FILE_PREFIX=proxy-tmp

cp $FILE_PREFIX/tools/deb/istio-iptables.sh ${BAZEL_TARGET_DIR}
cp $FILE_PREFIX/tools/deb/istio-start.sh ${BAZEL_TARGET_DIR}
cp $FILE_PREFIX/tools/deb/envoy.json ${BAZEL_TARGET_DIR}
cp $FILE_PREFIX/docker/proxy-* ${BAZEL_TARGET_DIR}
cp $FILE_PREFIX/docker/Dockerfile.debug ${BAZEL_TARGET_DIR}

rm -rf proxy-tmp

docker build -f ${BAZEL_TARGET_DIR}/Dockerfile.debug -t "${DEBUG_IMAGE_NAME}" ${BAZEL_TARGET_DIR}


docker push  "${DEBUG_IMAGE_NAME}"
