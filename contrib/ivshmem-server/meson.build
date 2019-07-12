executable('ivshmem-server', files('ivshmem-server.c', 'main.c'),
           dependencies: [qemuutil, rt],
           build_by_default: targetos == 'linux',
           install: false)
