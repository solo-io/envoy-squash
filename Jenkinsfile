def label = UUID.randomUUID().toString()

  podTemplate(label: label, containers: [
    containerTemplate(name: 'envoy', image: 'envoyproxy/envoy-build-ubuntu', ttyEnabled: true, command: 'cat'),
    containerTemplate(name: 'docker', image: 'docker:17.11', ttyEnabled: true, command: 'cat')
    ], envVars: [
        envVar(key: 'BRANCH_NAME', value: env.BRANCH_NAME),
        envVar(key: 'DOCKER_CONFIG', value: '/etc/docker'),
    ],
    volumes: [hostPathVolume(hostPath: '/var/run/docker.sock', mountPath: '/var/run/docker.sock'),
              secretVolume(secretName: 'soloio-docker-hub', mountPath: '/etc/docker'),],
    ) {

    node(label) {
      stage('Checkout') {
        checkout scm
        // git 'https://github.com/solo-io/envoy-squash'
      }
      stage('Setup go path') {
        container('envoy') {
          sh 'bazel build -c dbg //:envoy'
        }
      }
      stage('Build and push envoy container') {
        container('docker') {
            sh 'apk add --update git'
            sh 'sh build_docker.sh'
        }
      }
    }
  }
