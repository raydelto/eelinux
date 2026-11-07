"""Move the PS2 IOP IRQ relay's sleeping RPCs to IRQ resource callbacks.

The IRQ core calls startup/shutdown under desc->lock with IRQs disabled.
Resource acquisition/release runs in the requesting process instead, before
that lock is acquired or after it is released. This adapter accepts only the
reviewed post-API-port driver; it does not guess at changed source.
SPDX-License-Identifier: GPL-2.0-only
"""


_OLD_REQUEST = '''static unsigned int iop_irq_startup(struct irq_data *data)
{
	static bool irq_relay = false;

	const struct iop_rpc_relay_map arg = {
		.iop = data->irq - IOP_IRQ_BASE,
		.map = data->irq,
		.rpc = true,	/* FIXME: Also implement SMFLAG relay */
	};
	s32 status;
	int err;

	BUG_ON(in_hardirq());

	if (!irq_relay) {
		int id;

		/*
		 * The main reason for requesting the IOP IRQ relay module here
		 * instead of in irqrelay_init() is that now the console may be
		 * visible to print messages if there are problems.
		 */
		id = iop_module_request("irqrelay", 0x0100, NULL);
		if (id < 0)
			return id;

		err = sif_rpc_bind(&iop_irq_rpc, SIF_SID_IRQ_RELAY);
		if (err < 0) {
			pr_err("%s: sif_rpc_bind failed with %d\\n",
				__func__, err);
			return err;
		}

		irq_relay = true;
	}

	err = sif_rpc(&iop_irq_rpc, rpo_request_irq,
		&arg, sizeof(arg),
		&status, sizeof(status));

	pr_debug("%s: err %d status %d\\n", __func__, err, status);

	return err < 0 ? err : status;
}'''

_OLD_RELEASE = '''static void iop_irq_shutdown(struct irq_data *data)
{
	const struct iop_rpc_relay_release arg = {
		.iop = data->irq - IOP_IRQ_BASE,
	};
	s32 status;
	int err;

	BUG_ON(in_hardirq());

	err = sif_rpc(&iop_irq_rpc, rpo_release_irq,
		&arg, sizeof(arg),
		&status, sizeof(status));

	pr_debug("%s: err %d status %d\\n", __func__, err, status);
}'''

_NEW_REQUEST = '''static int iop_irq_request_resources(struct irq_data *data)
{
	const struct iop_rpc_relay_map arg = {
		.iop = data->irq - IOP_IRQ_BASE,
		.map = data->irq,
		.rpc = true,	/* FIXME: Also implement SMFLAG relay */
	};
	s32 status = 0;
	int err;

	/* This callback runs before the IRQ descriptor's raw spinlock. */
	mutex_lock(&iop_irq_lock);
	if (!irq_relay) {
		int id;

		id = iop_module_request("irqrelay", 0x0100, NULL);
		if (id < 0) {
			err = id;
			goto out;
		}

		err = sif_rpc_bind(&iop_irq_rpc, SIF_SID_IRQ_RELAY);
		if (err < 0) {
			pr_err("%s: sif_rpc_bind failed with %d\\n",
				__func__, err);
			goto out;
		}
		irq_relay = true;
	}

	err = sif_rpc(&iop_irq_rpc, rpo_request_irq,
		&arg, sizeof(arg),
		&status, sizeof(status));
	pr_debug("%s: err %d status %d\\n", __func__, err, status);
	if (err >= 0)
		err = status;
out:
	mutex_unlock(&iop_irq_lock);
	return err;
}'''

_NEW_RELEASE = '''static void iop_irq_release_resources(struct irq_data *data)
{
	const struct iop_rpc_relay_release arg = {
		.iop = data->irq - IOP_IRQ_BASE,
	};
	s32 status = 0;
	int err;

	/* The last free_irq() has dropped the descriptor lock and synced IRQs. */
	mutex_lock(&iop_irq_lock);
	err = sif_rpc(&iop_irq_rpc, rpo_release_irq,
		&arg, sizeof(arg),
		&status, sizeof(status));
	if (err < 0 || status)
		pr_warn("%s: relay release failed: RPC %d status %d\\n",
			__func__, err, status);
	mutex_unlock(&iop_irq_lock);
}'''


def modernize_iop_irq(text: str) -> str:
    """Return the adapted driver, rejecting missing/duplicated/changed anchors."""
    replacements = (
        ("#include <linux/module.h>\n", "#include <linux/module.h>\n#include <linux/mutex.h>\n"),
        ("static struct sif_rpc_client iop_irq_rpc;\n",
         "static struct sif_rpc_client iop_irq_rpc;\nstatic DEFINE_MUTEX(iop_irq_lock);\nstatic bool irq_relay;\n"),
        (_OLD_REQUEST, _NEW_REQUEST),
        (_OLD_RELEASE, _NEW_RELEASE),
        (".irq_startup = iop_irq_startup,", ".irq_request_resources = iop_irq_request_resources,"),
        (".irq_shutdown = iop_irq_shutdown,", ".irq_release_resources = iop_irq_release_resources,"),
    )
    for original, _ in replacements:
        if text.count(original) != 1:
            raise RuntimeError("Unexpected PS2 IOP IRQ source; review the relay resource adaptation")
    for original, updated in replacements:
        text = text.replace(original, updated, 1)
    return text
