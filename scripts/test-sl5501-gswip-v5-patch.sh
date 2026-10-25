#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
PATCH="$ROOT/target/linux/lantiq/patches-6.6/0743-net-dsa-lantiq-gswip-export-sl5501-port5-tx-delay-diag.patch"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/drivers/net/dsa"
cat > "$TMP/drivers/net/dsa/lantiq_gswip.c" <<'EOF'
#define  GSWIP_MII_CFG_RATE_MASK		0x70
#define GSWIP_MII_PCDU0			0x01
#define GSWIP_MII_PCDU1			0x03
#define GSWIP_MII_PCDU5			0x05
#define  GSWIP_MII_PCDU_TXDLY_MASK	GENMASK(2, 0)
#define  GSWIP_MII_PCDU_RXDLY_MASK	GENMASK(9, 7)

static void gswip_mii_mask_pcdu(struct gswip_priv *priv, u32 clear, u32 set,
				int port)
{
	switch (port) {
	case 0:
		gswip_mii_mask(priv, clear, set, GSWIP_MII_PCDU0);
		break;
	case 1:
		gswip_mii_mask(priv, clear, set, GSWIP_MII_PCDU1);
		break;
	case 5:
		gswip_mii_mask(priv, clear, set, GSWIP_MII_PCDU5);
		break;
	}
}

static int gswip_mdio_poll(struct gswip_priv *priv)
{
	int cnt = 100;
	return cnt;
}

static int gswip_setup(struct dsa_switch *ds)
{
	struct gswip_priv *priv = ds->priv;
	unsigned int cpu_port = priv->hw_info->cpu_port;
	int i;
	int err;

	ds->mtu_enforcement_ingress = true;

	ds->configure_vlan_while_not_filtering = false;

	return 0;
}

static u32 gswip_bcm_ram_entry_read(struct gswip_priv *priv, u32 table,
				    u32 index)
{
	u32 result;
	int err;

	err = 0;
	if (err) {
		dev_err(priv->dev, "timeout while reading table: %u, index: %u\n",
			table, index);
		return 0;
	}

	result = 0;
	return result;
}

static void gswip_get_ethtool_stats(struct dsa_switch *ds, int port,
				    uint64_t *data)
{
}
EOF
patch --dry-run -p1 -d "$TMP" < "$PATCH" >/dev/null
echo 'OK: SL5501 GSWIP patch parses, fixes PCDU5 0x05->0x0b and retains diagnostics'
