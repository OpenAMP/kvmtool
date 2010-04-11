#include "kvm/kvm.h"

#include "kvm/util.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void usage(char *argv[])
{
	fprintf(stderr, "  usage: %s [--single-step] [--kernel=]<kernel-image>\n",
		argv[0]);
	exit(1);
}

int main(int argc, char *argv[])
{
	const char *kernel_filename = NULL;
	const char *kernel_cmdline = NULL;
	bool single_step = false;
	struct kvm *kvm;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strncmp("--kernel=", argv[i], 9)) {
			kernel_filename = &argv[i][9];
			continue;
		} else if (!strncmp("--params=", argv[i], 9)) {
			kernel_cmdline = &argv[i][9];
			continue;
		} else if (!strncmp("--single-step", argv[i], 13)) {
			single_step = true;
			continue;
		} else {
			/* any unspecified arg is kernel image */
			if (argv[i][0] != '-')
				kernel_filename = argv[i];
			else
				warning("Unknown option: %s", argv[i]);
		}
	}

	/* at least we should have kernel image passed */
	if (!kernel_filename)
		usage(argv);

	kvm = kvm__init();

	kvm__setup_cpuid(kvm);

	if (!kvm__load_kernel(kvm, kernel_filename, kernel_cmdline))
		die("unable to load kernel %s", kernel_filename);

	kvm__reset_vcpu(kvm);

	if (single_step)
		kvm__enable_singlestep(kvm);

	for (;;) {
		kvm__run(kvm);

		switch (kvm->kvm_run->exit_reason) {
		case KVM_EXIT_DEBUG:
			kvm__show_registers(kvm);
			kvm__show_code(kvm);
			break;
		case KVM_EXIT_IO: {
			bool ret;

			ret = kvm__emulate_io(kvm,
					kvm->kvm_run->io.port,
					(uint8_t *)kvm->kvm_run + kvm->kvm_run->io.data_offset,
					kvm->kvm_run->io.direction,
					kvm->kvm_run->io.size,
					kvm->kvm_run->io.count);

			if (!ret)
				goto exit_kvm;
			break;
		}
		default:
			goto exit_kvm;
		}
	}

exit_kvm:
	fprintf(stderr, "KVM exit reason: %" PRIu32 " (\"%s\")\n",
		kvm->kvm_run->exit_reason, kvm_exit_reasons[kvm->kvm_run->exit_reason]);

	kvm__show_registers(kvm);
	kvm__show_code(kvm);
	kvm__show_page_tables(kvm);

	kvm__delete(kvm);

	return 0;
}