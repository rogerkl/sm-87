/* SM-87: the S700 sample dump and parameter messages, against a dump from a real X7000 and dumps the Java tool built
 * (Axs7's web/test/fixtures, ../Axs7SdsW), and the SPRM example in documentation/akai-s700-x7000/
 * 02-sample-parameters.md. Built and run by test/run.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sds.h"

static int fails, checks;
#define CHECK(cond, ...)                                                                                              \
    do {                                                                                                              \
        checks++;                                                                                                     \
        if (!(cond)) {                                                                                                \
            fails++;                                                                                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                               \
            printf(__VA_ARGS__);                                                                                      \
            printf("\n");                                                                                             \
        }                                                                                                             \
    } while (0)

static uint8_t mem0[S7_PACKED(S7_MAXWORDS)], mem1[S7_PACKED(S7_MAXWORDS)];
static const char *fixtures;

static uint8_t *load(const char *name, uint32_t *n)
{
    char path[512];
    FILE *f;
    uint8_t *b;
    long len;
    snprintf(path, sizeof path, "%s/%s", fixtures, name);
    if (!(f = fopen(path, "rb")))
        return 0;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)len);
    if (fread(b, 1, (size_t)len, f) != (size_t)len)
        len = 0;
    fclose(f);
    *n = (uint32_t)len;
    return b;
}

static sds_dump_t *feed(const uint8_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        sds_rx_byte(b[i]);
    for (i = 0; i < SDS_BUFS; i++)
        if (sds_buf[i].state == SDS_READY)
            return &sds_buf[i];
    return 0;
}

/* the words as offset binary, from the dump's own bytes */
static uint32_t word_in_dump(const uint8_t *b, uint32_t i)
{
    const uint8_t *w = b + SDS_HEADER + (i / 60u) * SDS_BLOCK + 1u + 2u * (i % 60u);
    return (uint32_t)w[0] << 5 | w[1] >> 2;
}

static void test_x7000(void)
{
    uint32_t n, i, bad = 0;
    uint8_t *b = load("x7000-received.syx", &n), p[S7_NPARAM];
    sds_dump_t *d;
    if (!b) {
        printf("skip: no %s/x7000-received.syx\n", fixtures);
        return;
    }
    sds_init(mem0, mem1);
    d = feed(b, n);
    CHECK(d != 0, "x7000: no dump");
    if (!d)
        return;
    CHECK(d->total == 32652u && d->blocks == 545u && d->bad_blocks == 0u && d->words == 32652u,
          "x7000: total %u blocks %u bad %u words %u", d->total, d->blocks, d->bad_blocks, d->words);
    CHECK(d->period_ns == 1815780u && sds_rate_code(d->period_ns) == 0u, "x7000: its garbage period %u", d->period_ns);
    for (i = 0; i < d->words; i++)
        bad += (uint32_t)(s7_w12(d->data, i) + 2048) != word_in_dump(b, i);
    CHECK(bad == 0, "x7000: %u words differ", bad);
    s7_default_params(p);
    p[S7_P_RECBW] = 76;
    sds_apply(d, p);
    CHECK(s7_word(p, S7_P_SEND) == 32652u && s7_word(p, S7_P_SSTRT) == 0u && p[S7_P_RPMODE] == S7_LOOP &&
              p[S7_P_RECBW] == 76u && p[S7_P_RECNP] == 60u,
          "x7000: after the dump END %u START %u mode %u rate %u root %u", s7_word(p, S7_P_SEND),
          s7_word(p, S7_P_SSTRT), p[S7_P_RPMODE], p[S7_P_RECBW], p[S7_P_RECNP]);
    /* sent back as the sampler would: byte for byte the same dump (START 0 to END, its loop, its rate) */
    {
        static uint8_t out[SDS_DUMP_MAX];
        static uint32_t at;
        s7_slot_t s;
        struct sink {
            uint8_t *b;
            uint32_t *at;
        } k = {out, &at};
        void put(const uint8_t *q, uint32_t m, void *ctx);
        memset(&s, 0, sizeof s);
        memcpy(s.p, p, S7_NPARAM);
        s.data = d->data;
        s.words = d->words;
        at = 0;
        sds_build(&s, 0, put, &k);
        CHECK(at == n, "x7000 sent back: %u bytes, the X7000's %u", at, n);
        /* the header but its period (the X7000's was garbage, ours is rate code 76's), and the last block but the
         * word after the announced total: the X7000 sent one more word of its memory there (its header counts
         * END - START, the word at END comes after; 01-sample-dump.md), where we pad with zero bytes; so its checksum */
        {
            uint32_t extra = SDS_HEADER + 544u * SDS_BLOCK + 1u + 2u * 12u;
            CHECK(!memcmp(out, b, 6) && !memcmp(out + 9, b + 9, SDS_HEADER - 9u) &&
                      !memcmp(out + 19, b + 19, extra - 19u) && !memcmp(out + extra + 2, b + extra + 2, n - 4u - extra) && out[n - 1u] == 0xF7u,
                  "x7000 sent back: the bytes differ");
        }
    }
    d->state = SDS_FREE;
    free(b);
}

void put(const uint8_t *q, uint32_t m, void *ctx)
{
    struct sink {
        uint8_t *b;
        uint32_t *at;
    } *k = ctx;
    memcpy(k->b + *k->at, q, m);
    *k->at += m;
}

static void test_java(void)
{
    uint32_t n;
    uint8_t *b = load("java-sine-forward.syx", &n), p[S7_NPARAM];
    sds_dump_t *d;
    if (!b) {
        printf("skip: no java-sine-forward.syx\n");
        return;
    }
    sds_init(mem0, mem1);
    d = feed(b, n);
    CHECK(d && d->total == 6200u && d->blocks == 103u && d->words == 6180u && d->loop_end == 5900u,
          "java forward: total %u blocks %u words %u loop end %u", d ? d->total : 0, d ? d->blocks : 0,
          d ? d->words : 0, d ? d->loop_end : 0);
    if (d) {
        s7_default_params(p);
        sds_apply(d, p);
        /* 200 more announced than sent, and the loop end is the end */
        CHECK(sds_length(d) == 5900u && s7_word(p, S7_P_SEND) == 5900u && p[S7_P_RECBW] == 76u,
              "java forward: length %u END %u rate %u", sds_length(d), s7_word(p, S7_P_SEND), p[S7_P_RECBW]);
        d->state = SDS_FREE;
    }
    free(b);
    b = load("java-sine-alternating.syx", &n);
    if (b) {
        d = feed(b, n);
        CHECK(d && d->alternating && d->loop_start == 1000u && d->loop_end == 31000u, "java alternating");
        if (d) {
            s7_default_params(p);
            sds_apply(d, p);
            CHECK(p[S7_P_RPMODE] == S7_ALT && s7_word(p, S7_P_SLOOP) == 1000u && s7_word(p, S7_P_SEND) == 31000u,
                  "java alternating: mode %u loop %u END %u", p[S7_P_RPMODE], s7_word(p, S7_P_SLOOP),
                  s7_word(p, S7_P_SEND));
            d->state = SDS_FREE;
        }
        free(b);
    }
}

static void test_buffers(void)
{
    /* two dumps in a row fill both buffers; a third is refused until one is written */
    static const uint8_t hdr[] = {0xF0, 0x7E, 0x01, 0x03, 0x00, 0x0C, 0x41, 0x45, 0x01,
                                  0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00};
    uint8_t blk[SDS_BLOCK];
    uint32_t i, k, chk = 0;
    sds_init(mem0, mem1);
    blk[0] = 0;
    for (i = 0; i < 120u; i++) {
        blk[1u + i] = (uint8_t)(i & 1u ? 0x00 : 0x40);
        chk ^= blk[1u + i];
    }
    blk[121] = (uint8_t)chk;
    for (k = 0; k < 3u; k++) {
        for (i = 0; i < sizeof hdr; i++)
            sds_rx_byte(hdr[i]);
        for (i = 0; i < SDS_BLOCK; i++)
            sds_rx_byte(blk[i]);
        sds_rx_byte(0xF7);
    }
    CHECK(sds_buf[0].state == SDS_READY && sds_buf[1].state == SDS_READY && sds_refused == 1u,
          "buffers: %u %u refused %u", sds_buf[0].state, sds_buf[1].state, sds_refused);
    CHECK(sds_buf[0].words == 60u && s7_w12(sds_buf[0].data, 5) == 0, "buffers: words %u", sds_buf[0].words);
    /* a dump cut short (another F0) leaves its buffer free */
    sds_buf[0].state = SDS_FREE;
    for (i = 0; i < sizeof hdr; i++)
        sds_rx_byte(hdr[i]);
    sds_rx_byte(0xF0);
    CHECK(sds_buf[0].state == SDS_FREE, "a dump cut short keeps its buffer");
    sds_rx_byte(0xF7);
}

static void test_sprm(void)
{
    /* the X7000's answer for slot 3 in 02-sample-parameters.md */
    uint8_t p[S7_NPARAM], q[S7_NPARAM], msg[SPRM_LEN];
    uint32_t i;
    s7_default_params(p);
    for (i = 0; i < 31u; i++) {
        p[S7_P_KEYL(i)] = 24;
        p[S7_P_KEYH(i)] = 0;
        p[S7_P_TRANSP(i)] = 0;
        p[S7_P_ATPTCH(i)] = 0xBC;
    }
    p[S7_P_KEYL(31)] = 60;
    p[S7_P_KEYH(31)] = 96;
    p[S7_P_TRANSP(31)] = 0;
    p[S7_P_ATPTCH(31)] = 0xBC;
    p[S7_P_UNDEF] = 2;
    p[S7_P_RPMODE] = 2;
    s7_set_word(p, S7_P_SSTRT, 0);
    s7_set_word(p, S7_P_LEN1, 31999);
    s7_set_word(p, S7_P_SEND, 32000);
    s7_set_word(p, S7_P_SLOOP, 0);
    s7_set_word(p, S7_P_LEN2, 31999);
    p[S7_P_LFSP] = 50;
    p[S7_P_LFDP] = 0;
    p[S7_P_LFDL] = 0;
    p[S7_P_FLTFRQ] = 99;
    p[S7_P_RLEASE] = 80;
    p[S7_P_LDNSS] = 99;
    p[S7_P_SFNTUN] = 0;
    p[S7_P_RECNP] = 72;
    p[S7_P_RECBW] = 76;
    p[S7_P_RVRSE] = 0;
    p[S7_P_VELFIL] = 0;
    p[S7_P_VELLDN] = 0;
    p[S7_P_VELRLS] = 99;
    p[153] = p[154] = 0;
    sprm_build(msg, 0, 2, p);
    {
        static const uint8_t head[] = {0xF0, 0x47, 0x00, 0x0A, 0x41, 0x02, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x01};
        static const uint8_t p32[] = {0x3C, 0x00, 0x60, 0x00, 0x00, 0x00, 0x3C, 0x01};
        static const uint8_t tail[] = {0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x01, 0x7C, 0x00,
                                       0x00, 0x00, 0x7D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x01, 0x7C, 0x00,
                                       0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x00, 0x50, 0x00, 0x63, 0x00,
                                       0x00, 0x00, 0x48, 0x00, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                       0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0xF7};
        CHECK(!memcmp(msg, head, sizeof head), "SPRM: the header and program 1");
        CHECK(!memcmp(msg + 7 + 31 * 8, p32, sizeof p32), "SPRM: program 32");
        CHECK(!memcmp(msg + 263, tail, sizeof tail), "SPRM: the sound and the checksum (%02X, the doc's 3C)", msg[317]);
    }
    CHECK(sprm_check(msg + 1, SPRM_LEN - 2u, 0) == 2, "SPRM: read back its slot");
    CHECK(sprm_check(msg + 1, SPRM_LEN - 2u, 1) == -1, "SPRM: another channel's");
    memset(q, 0, sizeof q);
    sprm_take(msg + 1, q);
    CHECK(!memcmp(p, q, S7_NPARAM), "SPRM: read back its values");
    msg[100] ^= 1u;
    CHECK(sprm_check(msg + 1, SPRM_LEN - 2u, 0) == -1, "SPRM: a wrong checksum");
    msg[100] ^= 1u;
    /* a second byte of 2 or more keeps the value */
    msg[7 + 2 * S7_P_FLTFRQ + 1] = 2;
    q[S7_P_FLTFRQ] = 33;
    sprm_take(msg + 1, q);
    CHECK(q[S7_P_FLTFRQ] == 33u, "SPRM: kept value");
    /* the overall settings, round trip */
    {
        uint8_t o[OVS_LEN];
        s7_ovs_t a = {3, 3, 3, 0, 2, 0, 5, 127}, b;
        memset(&b, 0, sizeof b);
        ovs_build(o, 3, &a);
        CHECK(o[0] == 0xF0 && o[3] == 0x08 && o[32] == 0xF7 && ovs_parse(o + 1, OVS_LEN - 2u, &b) == 0 &&
                  !memcmp(&a, &b, sizeof a),
              "OVS: round trip");
    }
}

int main(int argc, char **argv)
{
    fixtures = argc > 1 ? argv[1] : "../Axs7SdsW/web/test/fixtures";
    test_x7000();
    test_java();
    test_buffers();
    test_sprm();
    printf("sds: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
