# Native POSIX VM tests on the development host

The development host has installed NetBSD and OpenIndiana guests. The default
`virsh` connection is the user session, which does not list these guests. Use:

```sh
virsh -c qemu:///system list --all
```

Start a stopped guest with `virsh -c qemu:///system start netbsd` or
`virsh -c qemu:///system start openindiana`. Their existing SSH configurations
contain the forwarded ports, account and host-key settings:

```sh
ssh -F /mnt/nvme1/vms/netbsd/ssh_config netbsd uname -a
ssh -F /mnt/nvme1/vms/openindiana/ssh_config openindiana uname -a
```

Use a fresh temporary source directory in each guest; do not overwrite prior
checkouts or change VM configuration. Transfer the revision being tested with
`git archive` over SSH. NetBSD's `tar` reads the archive; OpenIndiana needs
`gtar` because its base `tar` rejects Git's global PAX header.

In the transferred source tree, build and run the complete suite:

```sh
gmake CC=gcc
gmake CC=gcc test
```

On NetBSD, put `/usr/pkg/gnu/bin` before the base utilities and use a UTF-8
locale (`LC_ALL=en_US.UTF-8`). The test environment also needs pkgsrc Bash,
GNU timeout, and a `python3` command for the installed Python version. This
host's guest already has the Python command in `$HOME/test-bin`:

```sh
PATH=$HOME/test-bin:/usr/pkg/gnu/bin:/usr/pkg/bin:/usr/bin:/bin
export PATH LC_ALL=en_US.UTF-8
```

On OpenIndiana, use `PATH=/usr/gnu/bin:/usr/bin:/usr/sbin:$PATH` for GNU tools.
The application enables illumos terminal extensions before system headers
and reads nanosecond file timestamps. The presenter suite tests same-second
rewrites and reads terminal settings from the PTY slave. Linux framebuffer
checks are skipped on both guests; the other terminal checks still run.

Also run a regression deck through `--check` and `--png-dir` at a fixed size.
Copy the PNGs back and compare them with the Linux export of the same deck.
Retain build/test logs, the exact revision and `uname -a` with the results.
