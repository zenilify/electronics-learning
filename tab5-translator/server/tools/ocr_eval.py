#!/usr/bin/env python3
"""OCR quality pass for /ocr-translate (lane N4, 2026-11-08). Run ON cachy (system python3: Pillow + numpy).

  python3 tools/ocr_eval.py make  ~/papp/n4/ocr-set          # 20 synthetic Tab5-angle signs + gt.json
  python3 tools/ocr_eval.py score ~/papp/n4/ocr-set [--base http://127.0.0.1:8787] [--engines ocr,vl,best]

Why synthetic: ground truth must be exact. Image-generation models redraw Chinese glyphs (a wrong
character in the "truth" poisons the score), so signs are typeset with Noto CJK (sans/serif, SC + TC),
then photographed-looking: perspective tilt 8-22 deg, scene background, glare, blur, sensor noise,
1280x720 JPEG q55-80 (Tab5 SC2356 preview class). Seeded -> reproducible; images stay out of git.

Metrics per engine: char accuracy = 1 - sum(min edit distance of each GT line to any returned line)/GT chars
(recall side); halluc = returned CJK chars not in the GT char bag / returned CJK chars (precision side);
adequacy = chrF(summary_es + item es, reference es) 0-100; latency ms. Bearer from ~/.config/zenilify/papp_token.
"""
from __future__ import annotations

import io, json, os, random, re, sys, time, urllib.request
from collections import Counter

SIGNS = [  # (category, lines zh, reference es, script)
    ("factory", ["安全第一", "预防为主"], "Seguridad primero. Prevención ante todo.", "sc"),
    ("factory", ["注意安全", "当心触电"], "Atención a la seguridad. Cuidado: riesgo de descarga eléctrica.", "sc"),
    ("factory", ["必须佩戴安全帽"], "Uso obligatorio de casco de seguridad.", "sc"),
    ("factory", ["禁止烟火"], "Prohibido fumar y encender fuego.", "sc"),
    ("factory", ["质检区", "不合格品 请勿使用"], "Zona de inspección de calidad. Producto no conforme, no usar.", "sc"),
    ("factory", ["紧急出口", "保持畅通"], "Salida de emergencia. Mantener despejada.", "sc"),
    ("factory", ["化学品仓库", "闲人免进"], "Almacén de productos químicos. Prohibido el paso a personas ajenas.", "sc"),
    ("menu", ["今日特价", "宫保鸡丁 28元", "麻婆豆腐 18元", "米饭 2元"],
     "Oferta del día. Pollo Kung Pao 28 yuanes. Tofu Mapo 18 yuanes. Arroz 2 yuanes.", "sc"),
    ("menu", ["菜单", "牛肉面 25元", "小笼包 15元", "酸辣汤 12元"],
     "Menú. Fideos con carne de res 25 yuanes. Xiaolongbao 15 yuanes. Sopa agripicante 12 yuanes.", "sc"),
    ("menu", ["饮品", "珍珠奶茶 16元", "柠檬水 10元", "热豆浆 5元"],
     "Bebidas. Té con leche y perlas 16 yuanes. Limonada 10 yuanes. Leche de soya caliente 5 yuanes.", "sc"),
    ("menu", ["本店不收现金", "请扫码支付"], "Este local no acepta efectivo. Pague escaneando el código.", "sc"),
    ("metro", ["2号线", "往 浦东国际机场 方向"], "Línea 2. Dirección Aeropuerto Internacional de Pudong.", "sc"),
    ("metro", ["出口 A", "人民广场"], "Salida A. Plaza del Pueblo.", "sc"),
    ("metro", ["换乘 1号线", "罗湖 方向"], "Transbordo a la Línea 1. Dirección Luohu.", "sc"),
    ("metro", ["请勿靠近屏蔽门"], "No se acerque a las puertas de andén.", "sc"),
    ("label", ["产品名称: 不锈钢螺栓", "规格: M8×30", "数量: 100件"],
     "Nombre del producto: perno de acero inoxidable. Especificación: M8×30. Cantidad: 100 piezas.", "sc"),
    ("label", ["生产日期: 2026年10月15日", "保质期: 12个月"],
     "Fecha de fabricación: 15 de octubre de 2026. Vida útil: 12 meses.", "sc"),
    ("label", ["注意: 易燃液体", "远离火源", "阴凉处保存"],
     "Atención: líquido inflamable. Mantener lejos de fuentes de fuego. Guardar en lugar fresco.", "sc"),
    ("label", ["电源适配器", "输入: 100-240V", "输出: 5V 3A", "中国制造"],
     "Adaptador de corriente. Entrada: 100-240 V. Salida: 5 V 3 A. Hecho en China.", "sc"),
    ("trad", ["歡迎光臨", "營業時間 10:00-22:00"], "Bienvenidos. Horario de atención: 10:00 a 22:00.", "tc"),
]
FONTS = ["/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Black.ttc",
         "/usr/share/fonts/noto-cjk/NotoSansCJK-Medium.ttc", "/usr/share/fonts/noto-cjk/NotoSerifCJK-SemiBold.ttc",
         "/usr/share/fonts/noto-cjk/NotoSerifCJK-Medium.ttc"]
PALETTE = {"factory": [((200, 20, 20), (255, 255, 255)), ((250, 200, 0), (0, 0, 0)), ((0, 120, 60), (255, 255, 255))],
           "menu": [((250, 240, 220), (120, 20, 10)), ((20, 20, 20), (255, 220, 120)), ((255, 255, 255), (0, 0, 0))],
           "metro": [((0, 70, 160), (255, 255, 255)), ((40, 40, 45), (255, 255, 255)), ((230, 0, 30), (255, 255, 255))],
           "label": [((255, 255, 255), (0, 0, 0)), ((235, 235, 225), (20, 20, 80))],
           "trad": [((150, 10, 10), (255, 215, 0))]}


def _coeffs(src, dst):
    import numpy as np
    A, b = [], []
    for (x, y), (u, v) in zip(dst, src):
        A += [[x, y, 1, 0, 0, 0, -u * x, -u * y], [0, 0, 0, x, y, 1, -v * x, -v * y]]
        b += [u, v]
    return np.linalg.solve(np.array(A, float), np.array(b, float)).tolist()


def make(out):
    import numpy as np
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
    os.makedirs(out, exist_ok=True)
    rnd, gt = random.Random(20261108), []
    for i, (cat, lines, ref, script) in enumerate(SIGNS, 1):
        bg, fg = rnd.choice(PALETTE[cat])
        font = ImageFont.truetype(rnd.choice(FONTS), rnd.randint(70, 110), index=3 if script == "tc" else 2)
        dims = [font.getbbox(t) for t in lines]
        w = max(d[2] - d[0] for d in dims) + 120
        lh = max(d[3] - d[1] for d in dims) + 36
        sign = Image.new("RGB", (w, lh * len(lines) + 90), bg)
        dr = ImageDraw.Draw(sign)
        dr.rectangle([12, 12, sign.width - 13, sign.height - 13], outline=fg, width=6)
        for k, t in enumerate(lines):
            x = (w - (dims[k][2] - dims[k][0])) // 2 if cat != "label" else 60
            dr.text((x, 45 + k * lh - dims[k][1]), t, font=font, fill=fg)
        W, H = 1280, 720
        scene = np.zeros((H, W, 3), np.float32)
        base = np.array([rnd.randint(40, 200) for _ in range(3)], np.float32)
        scene[:] = base + np.linspace(-40, 40, W)[None, :, None] + np.random.default_rng(i).normal(0, 12, (H, W, 3))
        scene = Image.fromarray(np.clip(scene, 0, 255).astype("uint8"))
        s = min(0.82 * W / sign.width, 0.75 * H / sign.height)
        sw, sh = int(sign.width * s), int(sign.height * s)
        sign = sign.resize((sw, sh), Image.LANCZOS)
        cx, cy = W // 2 + rnd.randint(-80, 80), H // 2 + rnd.randint(-40, 40)
        tilt, j = rnd.uniform(0.08, 0.22), lambda: rnd.uniform(-25, 25)
        x0, y0, x1, y1 = cx - sw // 2, cy - sh // 2, cx + sw // 2, cy + sh // 2
        dq = [(x0 + j(), y0 + j() + tilt * sh * 0.5), (x1 + j(), y0 + j() - tilt * sh * 0.3),
              (x1 + j(), y1 + j() + tilt * sh * 0.3), (x0 + j(), y1 + j() - tilt * sh * 0.5)]
        sq = [(0, 0), (sw, 0), (sw, sh), (0, sh)]
        warped = sign.transform((W, H), Image.PERSPECTIVE, _coeffs(sq, dq), Image.BICUBIC)
        mask = Image.new("L", (sw, sh), 255).transform((W, H), Image.PERSPECTIVE, _coeffs(sq, dq), Image.BICUBIC)
        scene.paste(warped, (0, 0), mask)
        glare = Image.new("L", (W, H), 0)
        gx, gy = rnd.randint(200, 1000), rnd.randint(100, 600)
        ImageDraw.Draw(glare).ellipse([gx - 220, gy - 90, gx + 220, gy + 90], fill=rnd.randint(40, 110))
        scene = Image.composite(Image.new("RGB", (W, H), (255, 255, 250)), scene, glare.filter(ImageFilter.GaussianBlur(70)))
        scene = scene.filter(ImageFilter.GaussianBlur(rnd.uniform(0.5, 1.4)))
        arr = np.asarray(scene, np.float32) + np.random.default_rng(100 + i).normal(0, 5, (H, W, 3))
        name = f"{i:02d}-{cat}.jpg"
        Image.fromarray(np.clip(arr, 0, 255).astype("uint8")).save(os.path.join(out, name), quality=rnd.randint(55, 80))
        gt.append({"file": name, "cat": cat, "script": script, "zh": lines, "ref_es": ref})
    json.dump(gt, open(os.path.join(out, "gt.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print(f"made {len(gt)} images in {out}")


CJK = re.compile(r"[㐀-鿿豈-﫿]")


def keep(s):
    return re.sub(r"[\s:：,，。·.]+", "", s).replace("×", "x").upper()


def lev(a, b):
    prev = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        cur = [i]
        for j, cb in enumerate(b, 1):
            cur.append(min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (ca != cb)))
        prev = cur
    return prev[-1]


def chrf(hyp, ref, n=6, beta=2.0):
    def grams(s, k):
        s = re.sub(r"\s+", " ", s.lower())
        return Counter(s[i:i + k] for i in range(len(s) - k + 1))
    P = R = 0.0
    m = 0
    for k in range(1, n + 1):
        h, r = grams(hyp, k), grams(ref, k)
        if not h or not r:
            continue
        ov = sum((h & r).values())
        P += ov / sum(h.values()); R += ov / sum(r.values()); m += 1
    if not m:
        return 0.0
    P, R = P / m, R / m
    return 0.0 if P + R == 0 else 100 * (1 + beta ** 2) * P * R / (beta ** 2 * P + R)


def call(base, tok, engine, path):
    r = urllib.request.Request(f"{base}/ocr-translate?engine={engine}", data=open(path, "rb").read(),
                               headers={"Authorization": f"Bearer {tok}", "Content-Type": "image/jpeg"})
    t = time.time()
    d = json.loads(urllib.request.urlopen(r, timeout=120).read())
    return d, int((time.time() - t) * 1000)


def score(out, base, engines):
    tok = open(os.path.expanduser("~/.config/zenilify/papp_token")).read().strip()
    gt = json.load(open(os.path.join(out, "gt.json"), encoding="utf-8"))
    res = {e: [] for e in engines}
    for g in gt:
        for e in engines:
            try:
                d, ms = call(base, tok, e, os.path.join(out, g["file"]))
            except Exception as ex:  # noqa: BLE001
                res[e].append({"file": g["file"], "err": type(ex).__name__}); print(g["file"], e, "ERR", type(ex).__name__)
                continue
            got = [keep(it.get("zh", "")) for it in d.get("items", [])]
            joined = "".join(got)
            dist = sum(min([lev(keep(l), x) for x in got] + [len(keep(l)), lev(keep(l), joined)]) for l in g["zh"])
            n = sum(len(keep(l)) for l in g["zh"])
            bag = Counter(CJK.findall("".join(g["zh"])))
            ret = CJK.findall(joined)
            extra = sum((Counter(ret) - bag).values())
            es = " ".join([d.get("summary_es") or ""] + [it.get("es", "") for it in d.get("items", [])])
            row = {"file": g["file"], "cat": g["cat"], "engine_used": d.get("engine"), "acc": 1 - dist / n,
                   "halluc": extra / max(1, len(ret)), "chrf": chrf(es, g["ref_es"]), "ms": ms,
                   "zh": [it.get("zh", "") for it in d.get("items", [])], "es": es[:200]}
            res[e].append(row)
            print(f"{g['file']:14} {e:4} used={row['engine_used']:4} acc={row['acc']:.2f} hal={row['halluc']:.2f} "
                  f"chrF={row['chrf']:.0f} {ms}ms | {' / '.join(row['zh'])[:60]}", flush=True)
            time.sleep(1.5)
    json.dump(res, open(os.path.join(out, "scores.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("\nSUMMARY engine | n_ok | char_acc | halluc | chrF_es | p50_ms | fell_back")
    for e, rows in res.items():
        ok = [r for r in rows if "acc" in r]
        if not ok:
            print(e, "no results"); continue
        ms = sorted(r["ms"] for r in ok)
        fb = sum(e not in (r["engine_used"] or "").split(":")[:2] for r in ok)  # "vl:<model>", "peer:best:<model>"
        print(f"{e:5} | {len(ok)}/{len(rows)} | {sum(r['acc'] for r in ok) / len(ok):.3f} | "
              f"{sum(r['halluc'] for r in ok) / len(ok):.3f} | {sum(r['chrf'] for r in ok) / len(ok):.1f} | "
              f"{ms[len(ms) // 2]} | {fb}")
        for c in sorted({r["cat"] for r in ok}):
            cr = [r for r in ok if r["cat"] == c]
            print(f"      {c:8} acc={sum(r['acc'] for r in cr) / len(cr):.3f} chrF={sum(r['chrf'] for r in cr) / len(cr):.1f}")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[0] == "make":
        make(os.path.expanduser(a[1]))
    else:
        base = a[a.index("--base") + 1] if "--base" in a else "http://127.0.0.1:8787"
        eng = a[a.index("--engines") + 1].split(",") if "--engines" in a else ["ocr", "vl", "best"]
        score(os.path.expanduser(a[1]), base, eng)
