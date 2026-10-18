// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * zappo-twin-bt: the Zappo X device twin's Bluetooth.
 *
 * The RG40XX H's Bluetooth is a Realtek chip on a serial port, attached at
 * start-up by rtk_hciattach.  On the twin there is no chip, so this stands in
 * for it: it makes hci0 through the kernel's virtual HCI driver (/dev/vhci)
 * and puts a few pretend devices in range for hci0 to find:
 *
 *   Zappo Twin Pad    a gamepad.  Pairs, connects and works: once connected it
 *                     is a real input device in the twin, and its buttons are
 *                     pressed from outside (twinctl.py btpad ...).
 *   Zappo Twin Buds   headphones.  Pair, but will not connect: the twin has no
 *                     Bluetooth audio (a real pair would play through A2DP).
 *   Zappo Twin Phone  a phone.  Shows up in a scan, like the phones around a
 *                     real handheld; nothing to connect to.
 *
 * Everything below the HCI line is BlueZ's own controller emulator
 * (emulator/btdev.c, emulator/bthost.c from BlueZ 5.79, the version Knulli
 * runs).  This file wires the pretend devices to it and answers for them:
 * their name, class, SDP records, the HID channels, and their link keys
 * (kept in KEYS so pairing survives a restart of the twin, like on a real
 * pad).
 *
 * Control: datagrams to /tmp/zappo-twin/bt.sock
 *   "pad HEX"      send a gamepad input report (7 bytes: buttons lo, hi, hat, x, y, z, rz)
 *   "wake"         the pad reconnects to the handheld on its own (as after power-on)
 *   "status"       answered with a line about each device
 *
 * Only exists inside the twin; nothing of it ships on a real handheld.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "lib/bluetooth.h"
#include "lib/hci.h"
#include "src/shared/mainloop.h"
#include "src/shared/util.h"
#include "monitor/bt.h"
#include "emulator/btdev.h"
#include "emulator/bthost.h"
#include "emulator/vhci.h"

#define DIR_RUN "/tmp/zappo-twin"
#define CTL_SOCK DIR_RUN "/bt.sock"
#define KEYS "/userdata/system/.zappo-twin/bt-keys"

static FILE *logf;

static void say(const char *fmt, ...)
{
	va_list ap;
	struct timespec ts;

	if (!logf)
		return;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	fprintf(logf, "%5ld.%03ld ", (long) ts.tv_sec, ts.tv_nsec / 1000000);
	va_start(ap, fmt);
	vfprintf(logf, fmt, ap);
	va_end(ap);
	fputc('\n', logf);
	fflush(logf);
}

/* ------------------------------------------------------------ the HID pad */

/* 16 buttons, a hat, four 8-bit axes (X, Y, Z, Rz): what a plain
 * Bluetooth gamepad reports, without any vendor's quirks */
static const uint8_t pad_report_desc[] = {
	0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,		/* Generic Desktop, Game Pad */
	0x05, 0x09, 0x19, 0x01, 0x29, 0x10,		/* Buttons 1-16 */
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
	0x05, 0x01, 0x09, 0x39,				/* Hat switch, 0-7, null 8 */
	0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3b, 0x01, 0x65, 0x14,
	0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
	0x65, 0x00, 0x75, 0x04, 0x95, 0x01, 0x81, 0x03,	/* padding */
	0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,	/* X Y Z Rz */
	0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02,
	0xc0
};
#define PAD_REPORT_LEN 7

/* --------------------------------------------------- SDP data elements */

struct buf {
	uint8_t d[1024];
	size_t n;
};

static void put(struct buf *b, const void *p, size_t n)
{
	if (b->n + n <= sizeof(b->d)) {
		memcpy(b->d + b->n, p, n);
		b->n += n;
	}
}

static void put8(struct buf *b, uint8_t v) { put(b, &v, 1); }
static void put16(struct buf *b, uint16_t v) { put8(b, v >> 8); put8(b, v); }
static void put32(struct buf *b, uint32_t v) { put16(b, v >> 16); put16(b, v); }

static void de_u8(struct buf *b, uint8_t v) { put8(b, 0x08); put8(b, v); }
static void de_u16(struct buf *b, uint16_t v) { put8(b, 0x09); put16(b, v); }
static void de_u32(struct buf *b, uint32_t v) { put8(b, 0x0a); put32(b, v); }
static void de_uuid16(struct buf *b, uint16_t v) { put8(b, 0x19); put16(b, v); }
static void de_bool(struct buf *b, bool v) { put8(b, 0x28); put8(b, v); }

static void de_text(struct buf *b, const void *s, size_t n)
{
	put8(b, 0x25);
	put8(b, n);
	put(b, s, n);
}

/* a sequence: header, then whatever is in inner */
static void de_seq(struct buf *b, const struct buf *inner)
{
	if (inner->n < 256) {
		put8(b, 0x35);
		put8(b, inner->n);
	} else {
		put8(b, 0x36);
		put16(b, inner->n);
	}
	put(b, inner->d, inner->n);
}

/* one service record: attribute ID -> encoded value */
struct attr {
	uint16_t id;
	struct buf val;
};

struct record {
	struct attr a[24];
	int n;
	uint16_t uuids[8];	/* the UUIDs a search can match it by */
	int nuuids;
};

static struct buf *rec_add(struct record *r, uint16_t id)
{
	struct attr *a = &r->a[r->n++];

	a->id = id;
	a->val.n = 0;
	return &a->val;
}

static void build_hid_record(struct record *r, const char *name)
{
	struct buf s1, s2, s3, s4;

	memset(r, 0, sizeof(*r));
	de_u32(rec_add(r, 0x0000), 0x00010000);		/* record handle */
	s1.n = 0; de_uuid16(&s1, 0x1124);			/* HID */
	de_seq(rec_add(r, 0x0001), &s1);
	/* protocols: L2CAP PSM 0x11 (control), HIDP */
	s2.n = 0; de_uuid16(&s2, 0x0100); de_u16(&s2, 0x0011);
	s3.n = 0; de_uuid16(&s3, 0x0011);
	s1.n = 0; de_seq(&s1, &s2); de_seq(&s1, &s3);
	de_seq(rec_add(r, 0x0004), &s1);
	s1.n = 0; de_uuid16(&s1, 0x1002);			/* public browse group */
	de_seq(rec_add(r, 0x0005), &s1);
	s1.n = 0; de_u16(&s1, 0x656e); de_u16(&s1, 0x006a); de_u16(&s1, 0x0100);
	de_seq(rec_add(r, 0x0006), &s1);
	s2.n = 0; de_uuid16(&s2, 0x1124); de_u16(&s2, 0x0101);
	s1.n = 0; de_seq(&s1, &s2);
	de_seq(rec_add(r, 0x0009), &s1);
	/* additional protocols: L2CAP PSM 0x13 (interrupt), HIDP */
	s2.n = 0; de_uuid16(&s2, 0x0100); de_u16(&s2, 0x0013);
	s3.n = 0; de_uuid16(&s3, 0x0011);
	s4.n = 0; de_seq(&s4, &s2); de_seq(&s4, &s3);
	s1.n = 0; de_seq(&s1, &s4);
	de_seq(rec_add(r, 0x000d), &s1);
	de_text(rec_add(r, 0x0100), name, strlen(name));
	de_u16(rec_add(r, 0x0201), 0x0111);		/* HID parser version */
	de_u8(rec_add(r, 0x0202), 0x08);		/* subclass: gamepad */
	de_u8(rec_add(r, 0x0203), 0x00);		/* country */
	de_bool(rec_add(r, 0x0204), true);		/* virtual cable */
	de_bool(rec_add(r, 0x0205), true);		/* the pad reconnects itself */
	s2.n = 0; de_u8(&s2, 0x22);
	de_text(&s2, pad_report_desc, sizeof(pad_report_desc));
	s1.n = 0; de_seq(&s1, &s2);
	de_seq(rec_add(r, 0x0206), &s1);		/* HID descriptor list */
	s2.n = 0; de_u16(&s2, 0x0409); de_u16(&s2, 0x0100);
	s1.n = 0; de_seq(&s1, &s2);
	de_seq(rec_add(r, 0x0207), &s1);
	de_u16(rec_add(r, 0x020b), 0x0100);		/* HID profile version */
	de_u16(rec_add(r, 0x020c), 0x0c80);		/* supervision timeout */
	de_bool(rec_add(r, 0x020d), false);		/* normally connectable */
	de_bool(rec_add(r, 0x020e), false);		/* boot device */

	r->uuids[r->nuuids++] = 0x1124;
	r->uuids[r->nuuids++] = 0x0100;
	r->uuids[r->nuuids++] = 0x0011;
	r->uuids[r->nuuids++] = 0x1002;
}

/* ------------------------------------------------------ pretend devices */

struct remote {
	const char *name;
	uint32_t cls;			/* class of device */
	uint8_t addr[6];		/* little-endian, as HCI has it */
	uint8_t io_cap;
	bool hid;

	struct btdev *dev;
	struct bthost *host;
	int sv[2];			/* dev <-> host */
	struct record rec;

	uint16_t handle;		/* ACL to the handheld, 0 = none */
	uint16_t sdp_cid, ctrl_cid, intr_cid;	/* the handheld's end, for sending */
	uint8_t peer[6];		/* who it last talked to */
	bool waking;
};

static struct remote remotes[] = {
	{ "Zappo Twin Pad",   0x002508, { 0x01, 0x00, 0x00, 0x54, 0x50, 0x5a }, 0x03, true },
	{ "Zappo Twin Buds",  0x240404, { 0x02, 0x00, 0x00, 0x54, 0x50, 0x5a }, 0x03, false },
	{ "Zappo Twin Phone", 0x5a020c, { 0x03, 0x00, 0x00, 0x54, 0x50, 0x5a }, 0x01, false },
};
#define NREMOTES (sizeof(remotes) / sizeof(remotes[0]))

static struct vhci *vhci;

/* the pad's own channels when it reconnects: data comes in on the cid it
 * picked (scid) and goes out to the handheld's (dcid) */
#define WAKE_CTRL_SCID 0x0050
#define WAKE_INTR_SCID 0x0051
static void wake_conn_rsp(struct remote *r, uint16_t scid, uint16_t dcid, uint16_t result);

static void addr_str(const uint8_t *a, char *s)
{
	sprintf(s, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
}

/* ------------------------------------------------------------ link keys */

/* KEYS: one line per pairing, "DEVICE-ADDR PEER-ADDR KEY-HEX TYPE" */
static bool key_load(const uint8_t *dev, const uint8_t *peer, uint8_t *key)
{
	char want[64], line[160], d[20], p[20], k[40];
	unsigned int t;
	bool found = false;
	FILE *f = fopen(KEYS, "r");

	if (!f)
		return false;
	addr_str(dev, d);
	addr_str(peer, p);
	snprintf(want, sizeof(want), "%s %s ", d, p);
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, want, strlen(want)))
			continue;
		if (sscanf(line + strlen(want), "%32s %u", k, &t) != 2 || strlen(k) != 32)
			continue;
		for (int i = 0; i < 16; i++) {
			unsigned int v;

			sscanf(k + 2 * i, "%2x", &v);
			key[i] = v;
		}
		found = true;
	}
	fclose(f);
	return found;
}

static void key_save(const uint8_t *dev, const uint8_t *peer, const uint8_t *key,
								uint8_t type)
{
	char d[20], p[20], line[160], keep[4096] = "";
	FILE *f;

	addr_str(dev, d);
	addr_str(peer, p);
	f = fopen(KEYS, "r");
	if (f) {			/* keep everyone else's */
		char pre[48];

		snprintf(pre, sizeof(pre), "%s %s ", d, p);
		while (fgets(line, sizeof(line), f))
			if (strncmp(line, pre, strlen(pre)) &&
					strlen(keep) + strlen(line) < sizeof(keep))
				strcat(keep, line);
		fclose(f);
	}
	mkdir("/userdata/system/.zappo-twin", 0755);
	f = fopen(KEYS ".tmp", "w");
	if (!f)
		return;
	fputs(keep, f);
	fprintf(f, "%s %s ", d, p);
	for (int i = 0; i < 16; i++)
		fprintf(f, "%02x", key[i]);
	fprintf(f, " %u\n", type);
	fclose(f);
	rename(KEYS ".tmp", KEYS);
}

/* ------------------------------------------------------------- transport */

static void send_dev(const struct iovec *iov, int iovlen, void *user_data)
{
	struct remote *r = user_data;		/* btdev -> its host */

	if (writev(r->sv[0], iov, iovlen) < 0)
		say("%s: dev->host: %s", r->name, strerror(errno));
}

static void send_host(const struct iovec *iov, int iovlen, void *user_data)
{
	struct remote *r = user_data;		/* host -> its btdev */

	if (writev(r->sv[1], iov, iovlen) < 0)
		say("%s: host->dev: %s", r->name, strerror(errno));
}

static void dev_cmd(struct remote *r, uint16_t opcode, const void *p, uint8_t len)
{
	uint8_t pkt[4 + 255];

	pkt[0] = BT_H4_CMD_PKT;
	put_le16(opcode, pkt + 1);
	pkt[3] = len;
	memcpy(pkt + 4, p, len);
	btdev_receive_h4(r->dev, pkt, 4 + len);
}

/* what the pretend device's controller says to its host; link keys are
 * looked after here, the rest goes to BlueZ's host emulator */
static void from_dev(int fd, uint32_t events, void *user_data)
{
	struct remote *r = user_data;
	uint8_t buf[4096];
	ssize_t n = read(fd, buf, sizeof(buf));

	if (n <= 0)
		return;
	if (buf[0] == BT_H4_EVT_PKT && n >= 3) {
		uint8_t code = buf[1];
		const uint8_t *p = buf + 3;

		if (code == BT_HCI_EVT_LINK_KEY_NOTIFY && n >= 3 + 23) {
			memcpy(r->peer, p, 6);
			key_save(r->addr, p, p + 6, p[22]);
			say("%s: paired, link key kept", r->name);
		} else if (code == BT_HCI_EVT_LINK_KEY_REQUEST && n >= 3 + 6) {
			struct bt_hci_cmd_link_key_request_reply rep;

			memcpy(rep.bdaddr, p, 6);
			if (key_load(r->addr, p, rep.link_key)) {
				dev_cmd(r, BT_HCI_CMD_LINK_KEY_REQUEST_REPLY, &rep, sizeof(rep));
				return;
			}
		} else if (code == BT_HCI_EVT_DISCONNECT_COMPLETE) {
			say("%s: disconnected", r->name);
			r->handle = r->sdp_cid = r->ctrl_cid = r->intr_cid = 0;
			r->waking = false;
		}
	}
	bthost_receive_h4(r->host, buf, n);

	/* L2CAP connection responses on the signalling channel (cid 1) */
	if (buf[0] == BT_H4_ACL_PKT && n >= 9 && get_le16(buf + 7) == 0x0001) {
		const uint8_t *c = buf + 9, *end = buf + n;

		while (c + 4 <= end) {
			uint16_t clen = get_le16(c + 2);

			if (c[0] == BT_L2CAP_PDU_CONN_RSP && clen >= 8 && c + 4 + 8 <= end)
				wake_conn_rsp(r, get_le16(c + 6), get_le16(c + 4),
							get_le16(c + 8));
			c += 4 + clen;
		}
	}
}

static void from_host(int fd, uint32_t events, void *user_data)
{
	struct remote *r = user_data;
	uint8_t buf[4096];
	ssize_t n = read(fd, buf, sizeof(buf));

	if (n > 0)
		btdev_receive_h4(r->dev, buf, n);
}

/* ------------------------------------------------------------------ SDP */

static bool rec_matches(const struct record *rec, const uint16_t *want, int n)
{
	for (int i = 0; i < n; i++) {
		bool ok = false;

		for (int j = 0; j < rec->nuuids; j++)
			if (rec->uuids[j] == want[i])
				ok = true;
		if (!ok)
			return false;
	}
	return rec->n > 0;
}

/* reads a data element header; returns the size of its content */
static int de_hdr(const uint8_t **p, const uint8_t *end, uint8_t *type)
{
	uint8_t h;
	int n;

	if (*p >= end)
		return -1;
	h = *(*p)++;
	*type = h >> 3;
	switch (h & 7) {
	case 0: n = (*type == 0) ? 0 : 1; break;
	case 1: n = 2; break;
	case 2: n = 4; break;
	case 3: n = 8; break;
	case 4: n = 16; break;
	case 5: if (*p + 1 > end) return -1; n = **p; *p += 1; break;
	case 6: if (*p + 2 > end) return -1; n = get_be16(*p); *p += 2; break;
	default: if (*p + 4 > end) return -1; n = get_be32(*p); *p += 4; break;
	}
	return (*p + n <= end) ? n : -1;
}

/* the UUIDs in a search pattern (16-bit, or 128-bit on the Bluetooth base) */
static int parse_uuids(const uint8_t **p, const uint8_t *end, uint16_t *out, int max)
{
	uint8_t t;
	int n = de_hdr(p, end, &t), k = 0;
	const uint8_t *e;

	if (n < 0 || t != 6)
		return -1;
	e = *p + n;
	while (*p < e) {
		int m = de_hdr(p, e, &t);

		if (m < 0 || t != 3)
			return -1;
		if (k < max) {
			if (m == 2)
				out[k++] = get_be16(*p);
			else if (m == 4)
				out[k++] = get_be32(*p) & 0xffff;
			else if (m == 16)
				out[k++] = get_be16(*p + 2);
		}
		*p += m;
	}
	return k;
}

/* attribute ID list: ranges lo..hi */
static int parse_attr_ranges(const uint8_t **p, const uint8_t *end,
					uint16_t (*rng)[2], int max)
{
	uint8_t t;
	int n = de_hdr(p, end, &t), k = 0;
	const uint8_t *e;

	if (n < 0 || t != 6)
		return -1;
	e = *p + n;
	while (*p < e) {
		int m = de_hdr(p, e, &t);

		if (m < 0 || t != 1)
			return -1;
		if (k < max) {
			if (m == 2) {
				rng[k][0] = rng[k][1] = get_be16(*p);
				k++;
			} else if (m == 4) {
				rng[k][0] = get_be16(*p);
				rng[k][1] = get_be16(*p + 2);
				k++;
			}
		}
		*p += m;
	}
	return k;
}

static void rec_attrs(const struct record *rec, uint16_t (*rng)[2], int nr,
							struct buf *out)
{
	struct buf in = { .n = 0 };

	for (int i = 0; i < rec->n; i++) {
		const struct attr *a = &rec->a[i];

		for (int j = 0; j < nr; j++)
			if (a->id >= rng[j][0] && a->id <= rng[j][1]) {
				de_u16(&in, a->id);
				put(&in, a->val.d, a->val.n);
				break;
			}
	}
	de_seq(out, &in);
}

static void sdp_reply(struct remote *r, uint8_t pdu, uint16_t tid, const struct buf *body)
{
	uint8_t out[1100];

	out[0] = pdu;
	put_be16(tid, out + 1);
	put_be16(body->n, out + 3);
	memcpy(out + 5, body->d, body->n);
	bthost_send_cid(r->host, r->handle, r->sdp_cid, out, 5 + body->n);
}

static void sdp_error(struct remote *r, uint16_t tid, uint16_t code)
{
	struct buf b = { .n = 0 };

	put16(&b, code);
	sdp_reply(r, 0x01, tid, &b);
}

static void sdp_request(const void *data, uint16_t len, void *user_data)
{
	struct remote *r = user_data;
	const uint8_t *p = data, *end = p + len;
	uint16_t uuids[12], rng[16][2], tid;
	int nu, nr;
	uint8_t pdu;
	struct buf b = { .n = 0 }, lists = { .n = 0 };
	bool match;

	if (len < 5)
		return;
	pdu = p[0];
	tid = get_be16(p + 1);
	p += 5;
	switch (pdu) {
	case 0x02:	/* service search */
		nu = parse_uuids(&p, end, uuids, 12);
		if (nu < 0 || p + 2 > end)
			return sdp_error(r, tid, 0x0003);
		match = rec_matches(&r->rec, uuids, nu);
		put16(&b, match);
		put16(&b, match);
		if (match)
			put32(&b, 0x00010000);
		put8(&b, 0);
		return sdp_reply(r, 0x03, tid, &b);
	case 0x04:	/* service attribute */
		if (p + 6 > end)
			return sdp_error(r, tid, 0x0003);
		if (get_be32(p) != 0x00010000 || !r->rec.n)
			return sdp_error(r, tid, 0x0002);
		p += 6;
		nr = parse_attr_ranges(&p, end, rng, 16);
		if (nr < 0)
			return sdp_error(r, tid, 0x0003);
		rec_attrs(&r->rec, rng, nr, &lists);
		put16(&b, lists.n);
		put(&b, lists.d, lists.n);
		put8(&b, 0);
		return sdp_reply(r, 0x05, tid, &b);
	case 0x06:	/* service search attribute */
		nu = parse_uuids(&p, end, uuids, 12);
		if (nu < 0 || p + 2 > end)
			return sdp_error(r, tid, 0x0003);
		p += 2;
		nr = parse_attr_ranges(&p, end, rng, 16);
		if (nr < 0)
			return sdp_error(r, tid, 0x0003);
		{
			struct buf all = { .n = 0 };

			if (rec_matches(&r->rec, uuids, nu))
				rec_attrs(&r->rec, rng, nr, &all);
			de_seq(&lists, &all);
		}
		put16(&b, lists.n);
		put(&b, lists.d, lists.n);
		put8(&b, 0);
		return sdp_reply(r, 0x07, tid, &b);
	default:
		return sdp_error(r, tid, 0x0003);
	}
}

static void sdp_connect(uint16_t handle, uint16_t cid, void *user_data)
{
	struct remote *r = user_data;

	r->handle = handle;
	r->sdp_cid = cid;
	bthost_add_cid_hook(r->host, handle, cid, sdp_request, r);
}

/* ------------------------------------------------------------------ HID */

static void hid_control(const void *data, uint16_t len, void *user_data)
{
	struct remote *r = user_data;
	const uint8_t *p = data;
	uint8_t rep[1 + PAD_REPORT_LEN] = { 0xa1, 0, 0, 8, 0x80, 0x80, 0x80, 0x80 };
	uint8_t ok = 0x00;	/* HANDSHAKE: successful */

	if (!len)
		return;
	switch (p[0] >> 4) {
	case 0x4:		/* GET_REPORT: the pad at rest */
		bthost_send_cid(r->host, r->handle, r->ctrl_cid, rep, sizeof(rep));
		break;
	case 0x6:		/* GET_PROTOCOL: report protocol */
		rep[0] = 0xa0;
		rep[1] = 0x01;
		bthost_send_cid(r->host, r->handle, r->ctrl_cid, rep, 2);
		break;
	case 0x5: case 0x7: case 0x9:	/* SET_REPORT, SET_PROTOCOL, SET_IDLE */
		bthost_send_cid(r->host, r->handle, r->ctrl_cid, &ok, 1);
		break;
	case 0x1:		/* HID_CONTROL (e.g. virtual cable unplug) */
		say("%s: HID control 0x%02x", r->name, p[0]);
		break;
	}
}

static void hid_interrupt(const void *data, uint16_t len, void *user_data)
{
	/* output reports (rumble, lights): nothing on this pad */
}

static void ctrl_connect(uint16_t handle, uint16_t cid, void *user_data)
{
	struct remote *r = user_data;

	r->handle = handle;
	r->ctrl_cid = cid;
	bthost_add_cid_hook(r->host, handle, cid, hid_control, r);
	say("%s: HID control channel open", r->name);
}

static void intr_connect(uint16_t handle, uint16_t cid, void *user_data)
{
	struct remote *r = user_data;

	r->handle = handle;
	r->intr_cid = cid;
	bthost_add_cid_hook(r->host, handle, cid, hid_interrupt, r);
	say("%s: connected (HID interrupt channel open)", r->name);
}

static void hid_closed(void *user_data)
{
	struct remote *r = user_data;

	r->ctrl_cid = r->intr_cid = 0;
}

static void new_conn(uint16_t handle, void *user_data)
{
	struct remote *r = user_data;

	r->handle = handle;
	say("%s: link up (handle %u)", r->name, handle);
}

/* --------------------------------------------- the pad reconnecting itself */

/* the handheld's answer to one of the pad's channel requests; seen by
 * from_dev, since BlueZ's host emulator only reports the first answer and the
 * handheld first says "pending" while bluetoothd decides */
static void wake_conn_rsp(struct remote *r, uint16_t scid, uint16_t dcid, uint16_t result)
{
	if (!r->waking || (scid != WAKE_CTRL_SCID && scid != WAKE_INTR_SCID))
		return;
	if (result == 0x0001)		/* pending */
		return;
	if (result) {
		say("%s: the handheld refused the channel (%u)", r->name, result);
		r->waking = false;
		return;
	}
	if (scid == WAKE_CTRL_SCID) {
		struct bt_l2cap_pdu_conn_req req = { .psm = cpu_to_le16(0x0013),
						     .scid = cpu_to_le16(WAKE_INTR_SCID) };

		r->ctrl_cid = dcid;
		bthost_add_cid_hook(r->host, r->handle, WAKE_CTRL_SCID, hid_control, r);
		bthost_l2cap_req(r->host, r->handle, BT_L2CAP_PDU_CONN_REQ, &req,
							sizeof(req), NULL, NULL);
	} else {
		r->intr_cid = dcid;
		bthost_add_cid_hook(r->host, r->handle, WAKE_INTR_SCID, hid_interrupt, r);
		r->waking = false;
		say("%s: reconnected by itself", r->name);
	}
}

static void wake_auth_done(int id, void *user_data)
{
	struct remote *r = user_data;
	struct bt_l2cap_pdu_conn_req req = { .psm = cpu_to_le16(0x0011),
					     .scid = cpu_to_le16(WAKE_CTRL_SCID) };

	mainloop_remove_timeout(id);
	if (!r->handle) {
		r->waking = false;
		return;
	}
	bthost_l2cap_req(r->host, r->handle, BT_L2CAP_PDU_CONN_REQ, &req, sizeof(req),
								NULL, NULL);
}

static void wake_conn(uint16_t handle, void *user_data)
{
	struct remote *r = user_data;

	new_conn(handle, user_data);
	if (!r->waking)
		return;
	bthost_request_auth(r->host, handle);	/* encrypt with the kept key */
	mainloop_add_timeout(1500, wake_auth_done, r, NULL);
}

static void wake(struct remote *r)
{
	static const uint8_t none[6];

	if (r->handle || !memcmp(r->peer, none, 6)) {
		say("%s: wake: %s", r->name, r->handle ? "already connected" : "never paired");
		return;
	}
	r->waking = true;
	r->ctrl_cid = r->intr_cid = 0;
	bthost_hci_connect(r->host, r->peer, BDADDR_BREDR);
}

/* -------------------------------------------------------------- control */

static void control(int fd, uint32_t events, void *user_data)
{
	char msg[512], out[1024] = "";
	struct sockaddr_un from;
	socklen_t flen = sizeof(from);
	ssize_t n = recvfrom(fd, msg, sizeof(msg) - 1, 0, (void *) &from, &flen);
	struct remote *pad = &remotes[0];

	if (n <= 0)
		return;
	msg[n] = 0;
	if (!strncmp(msg, "pad ", 4)) {
		uint8_t rep[1 + PAD_REPORT_LEN] = { 0xa1 };

		for (int i = 0; i < PAD_REPORT_LEN; i++) {
			unsigned int v;

			if (sscanf(msg + 4 + 2 * i, "%2x", &v) != 1)
				return;
			rep[1 + i] = v;
		}
		if (pad->intr_cid) {
			bthost_send_cid(pad->host, pad->handle, pad->intr_cid, rep, sizeof(rep));
			strcpy(out, "ok\n");
		} else
			strcpy(out, "the pad is not connected\n");
	} else if (!strncmp(msg, "wake", 4)) {
		wake(pad);
		strcpy(out, "ok\n");
	} else if (!strncmp(msg, "status", 6)) {
		for (size_t i = 0; i < NREMOTES; i++) {
			struct remote *r = &remotes[i];
			char a[20], line[160];

			addr_str(r->addr, a);
			snprintf(line, sizeof(line), "%s %s %s\n", a, r->name,
				r->intr_cid ? "connected" : r->handle ? "linked" : "idle");
			strncat(out, line, sizeof(out) - strlen(out) - 1);
		}
	} else
		strcpy(out, "? pad HEX | wake | status\n");
	if (flen > sizeof(sa_family_t))
		sendto(fd, out, strlen(out), 0, (void *) &from, flen);
}

/* ----------------------------------------------------------------- start */

static void remote_start(struct remote *r, uint16_t id)
{
	struct bt_hci_cmd_write_local_name nm;
	uint8_t cls[3] = { r->cls, r->cls >> 8, r->cls >> 16 };
	uint8_t eir[241] = { 0 };
	size_t l = strlen(r->name);

	r->dev = btdev_create(BTDEV_TYPE_BREDRLE, id);
	r->host = bthost_create();
	if (!r->dev || !r->host || socketpair(AF_UNIX, SOCK_SEQPACKET |
				SOCK_NONBLOCK | SOCK_CLOEXEC, 0, r->sv) < 0) {
		say("%s: could not be made", r->name);
		exit(1);
	}
	btdev_set_bdaddr(r->dev, r->addr);
	btdev_set_send_handler(r->dev, send_dev, r);
	bthost_set_send_handler(r->host, send_host, r);
	mainloop_add_fd(r->sv[1], EPOLLIN, from_dev, r, NULL);	/* what dev wrote on sv[0] */
	mainloop_add_fd(r->sv[0], EPOLLIN, from_host, r, NULL);

	/* name, class and inquiry data, straight into its controller */
	memset(&nm, 0, sizeof(nm));
	memcpy(nm.name, r->name, l);
	dev_cmd(r, BT_HCI_CMD_WRITE_LOCAL_NAME, &nm, sizeof(nm));
	dev_cmd(r, BT_HCI_CMD_WRITE_CLASS_OF_DEV, cls, 3);
	eir[1] = l + 1;
	eir[2] = 0x09;			/* complete local name */
	memcpy(eir + 3, r->name, l);
	dev_cmd(r, BT_HCI_CMD_WRITE_EXT_INQUIRY_RESPONSE, eir, sizeof(eir));

	bthost_set_io_capability(r->host, r->io_cap);
	bthost_set_auth_req(r->host, 0x04);		/* general bonding */
	bthost_set_connect_cb(r->host, r->hid ? wake_conn : new_conn, r);
	if (r->hid) {
		build_hid_record(&r->rec, r->name);
		bthost_add_l2cap_server(r->host, 0x0011, ctrl_connect, hid_closed, r);
		bthost_add_l2cap_server(r->host, 0x0013, intr_connect, hid_closed, r);
	}
	bthost_add_l2cap_server(r->host, 0x0001, sdp_connect, NULL, r);	/* SDP */
	bthost_start(r->host);
	bthost_write_ssp_mode(r->host, 0x01);
	bthost_write_scan_enable(r->host, 0x03);	/* discoverable, connectable */
}

/* who the pad last paired with, so "wake" knows where to go */
static void remember_peer(struct remote *r)
{
	char line[160], a[20];
	FILE *f = fopen(KEYS, "r");

	if (!f)
		return;
	addr_str(r->addr, a);
	while (fgets(line, sizeof(line), f)) {
		unsigned int b[6];

		if (strncmp(line, a, 17) || sscanf(line + 18, "%2x:%2x:%2x:%2x:%2x:%2x",
				&b[5], &b[4], &b[3], &b[2], &b[1], &b[0]) != 6)
			continue;
		for (int i = 0; i < 6; i++)
			r->peer[i] = b[i];
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	int cs;

	mkdir(DIR_RUN, 0755);
	logf = fopen(DIR_RUN "/bt.log", "a");
	mainloop_init();

	vhci = vhci_open(BTDEV_TYPE_BREDRLE);
	if (!vhci) {
		say("no /dev/vhci: %s", strerror(errno));
		return 1;
	}
	say("hci0 is up (virtual)");
	for (size_t i = 0; i < NREMOTES; i++) {
		remote_start(&remotes[i], 0x100 + i);
		remember_peer(&remotes[i]);
		say("in range: %s", remotes[i].name);
	}

	cs = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	strcpy(sa.sun_path, CTL_SOCK);
	unlink(CTL_SOCK);
	if (cs < 0 || bind(cs, (void *) &sa, sizeof(sa)) < 0)
		say("control socket: %s", strerror(errno));
	else
		mainloop_add_fd(cs, EPOLLIN, control, NULL, NULL);

	return mainloop_run();
}
