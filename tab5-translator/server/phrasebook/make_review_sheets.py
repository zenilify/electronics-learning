#!/usr/bin/env python3
"""Build native-speaker review spreadsheets from phrases.psv (one file per reviewer).
Usage: python3 make_review_sheets.py OUT_DIR Name1 Name2 Name3
Rows are de-duplicated by Chinese text, split evenly (by order) across reviewers."""
import sys, os
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill, Alignment, Border, Side
from openpyxl.worksheet.datavalidation import DataValidation
from pypinyin import pinyin, Style

HERE = os.path.dirname(os.path.abspath(__file__))
out_dir, names = sys.argv[1], sys.argv[2:]
CAT = {"basico": "Basics 基本", "comida": "Food 餐饮", "bebidas": "Drinks 饮料", "transporte": "Transport 交通",
       "hotel": "Hotel 酒店", "compras": "Shopping 购物", "electronica": "Electronics 电子", "salud": "Health 健康",
       "emergencia": "Emergency 紧急", "tiempo": "Time & numbers 时间数字", "reuniones": "Meetings 会议",
       "fabrica": "Factory visit 工厂参观", "demo": "M5Stack demo 演示", "charla": "Small talk 闲聊",
       "lugares": "Places 地点", "negocios": "Business 商务"}

rows, seen = [], set()
for line in open(os.path.join(HERE, "phrases.psv"), encoding="utf-8"):
    if line.startswith("#") or "|" not in line:
        continue
    p = [x.strip() for x in line.rstrip("\n").split("|")]
    p += [""] * (5 - len(p))
    cat, es, en, zh, py = p[:5]
    if zh in seen:
        continue
    seen.add(zh)
    if not py:
        py = " ".join(s[0] for s in pinyin(zh, style=Style.TONE, heteronym=False))
    rows.append((CAT.get(cat, cat), en, es, zh, py))

n = len(rows)
k = len(names)
chunks = [rows[i * n // k:(i + 1) * n // k] for i in range(k)]
head = Font(bold=True, color="FFFFFF")
fill = PatternFill("solid", fgColor="29397A")
okfill = PatternFill("solid", fgColor="FFF4E0")
thin = Side(style="thin", color="CCCCCC")
os.makedirs(out_dir, exist_ok=True)

for name, chunk, idx in zip(names, chunks, range(k)):
    wb = Workbook()
    ins = wb.active
    ins.title = "How to review 说明"
    lines = [
        f"PAPP phrasebook — review for {name}",
        "",
        "Thank you so much for helping! / 非常感谢您的帮助！",
        "",
        "These short phrases will be spoken by a translator I built on an M5Stack Tab5, for my trip to Shanghai and Shenzhen.",
        "这些短句会由我用 M5Stack Tab5 做的翻译器播放，用于我在上海和深圳的行程。",
        "",
        "For each row on the next sheet, please look only at the CHINESE column (and the pinyin if you like):",
        "请只看“中文”一栏（如愿意也可以看拼音）：",
        "  1. If it is natural and correct for mainland China → choose  OK  in column G.",
        "     如果自然、正确（大陆用法）→ 在 G 栏选择  OK",
        "  2. If it sounds wrong, unnatural or too formal → choose  FIX  and write a better Chinese phrase in column H.",
        "     如果不对、不自然或太正式 → 选择  FIX，并在 H 栏写更好的说法",
        "  3. Column I is for any short comment (optional).  I 栏可写备注（可选）",
        "",
        f"There are {len(chunk)} phrases. Most people need about {max(10, round(len(chunk) * 10 / 60 / 5) * 5)} minutes.",
        f"共 {len(chunk)} 句，大约需要 {max(10, round(len(chunk) * 10 / 60 / 5) * 5)} 分钟。",
        "",
        "When you are done, just reply to my email with this file attached (or a photo / WeChat message with the numbers that need fixing).",
        "完成后请回复邮件并附上此文件（或用微信发需要修改的编号）。谢谢！— Erick",
    ]
    for i, t in enumerate(lines, 1):
        c = ins.cell(row=i, column=1, value=t)
        if i == 1:
            c.font = Font(bold=True, size=14, color="29397A")
    ins.column_dimensions["A"].width = 120

    ws = wb.create_sheet("Phrases 短句")
    hdr = ["No.", "Topic 类别", "English", "Español", "中文 (please check)", "Pinyin 拼音",
           "OK / FIX", "Better Chinese 更好的说法", "Comment 备注"]
    ws.append(hdr)
    for c in ws[1]:
        c.font, c.fill = head, fill
        c.alignment = Alignment(vertical="center", wrap_text=True)
    letter = "ABC"[idx] if idx < 3 else str(idx + 1)
    for j, (cat, en, es, zh, py) in enumerate(chunk, 1):
        ws.append([f"{letter}{j:03d}", cat, en, es, zh, py, "", "", ""])
    widths = [7, 18, 34, 34, 30, 34, 10, 34, 28]
    for i, w in enumerate(widths):
        ws.column_dimensions[chr(65 + i)].width = w
    dv = DataValidation(type="list", formula1='"OK,FIX"', allow_blank=True)
    ws.add_data_validation(dv)
    dv.add(f"G2:G{len(chunk) + 1}")
    for r in ws.iter_rows(min_row=2, max_row=len(chunk) + 1):
        for c in r:
            c.border = Border(top=thin, bottom=thin, left=thin, right=thin)
            c.alignment = Alignment(vertical="center", wrap_text=True)
        r[4].font = Font(size=13)
        for c in r[6:9]:
            c.fill = okfill
    ws.freeze_panes = "C2"
    fn = os.path.join(out_dir, f"PAPP-phrase-review-{letter}-{name}.xlsx")
    wb.save(fn)
    print(fn, len(chunk))
print("unique phrases:", n)
