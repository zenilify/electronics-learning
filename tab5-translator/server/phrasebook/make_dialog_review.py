#!/usr/bin/env python3
"""Native-speaker check of the 4 conversation test clips (z16-1..4) — one xlsx per reviewer + m4a audio.
Usage (MacBook): python3 make_dialog_review.py CLIPS_DIR OUT_DIR Rain Daniel James
Every reviewer gets ALL 4 clips (3 independent listeners -> majority on what was really said)."""
import os, subprocess, sys
from openpyxl import Workbook
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from openpyxl.worksheet.datavalidation import DataValidation

clips_dir, out_dir, names = sys.argv[1], sys.argv[2], sys.argv[3:]
# heard = whisper (our main recogniser); en = what the translator produced (mt=llm, server v0.2.1, 2026-11-08)
CLIPS = [
    ("z16-1", "你好,欢迎来到深圳。你是第一次来中国吗?", "Hello, welcome to Shenzhen. Is this your first time in China?"),
    ("z16-2", "这个开发版多少钱如果你卖10个我可以给你便宜点", "How much is this dev board? If you buy ten, I can give you a discount."),
    ("z16-3", "地铁站就在前面,往右走五分钟就到了。", "The subway station is just ahead; turn right and walk for five minutes to get there."),
    ("z16-4", "我们公司对你的翻译器很感兴趣,下个月可以一起合作吗?", "Our company is very interested in your translator. Can we cooperate next month?"),
]
os.makedirs(os.path.join(out_dir, "audio"), exist_ok=True)
for cid, _, _ in CLIPS:   # m4a plays inside WeChat; afconvert is built into macOS
    src, dst = os.path.join(clips_dir, f"{cid}.wav"), os.path.join(out_dir, "audio", f"PAPP-{cid}.m4a")
    # 16 kHz mono: AAC at 32 kbps (96 kbps is refused by afconvert for this rate); overwrites
    subprocess.run(["afconvert", "-f", "m4af", "-d", "aac", "-b", "32000", src, dst], check=True)

head, fill = Font(bold=True, color="FFFFFF"), PatternFill("solid", fgColor="29397A")
ans = PatternFill("solid", fgColor="FFF4E0")
thin = Side(style="thin", color="CCCCCC")
for idx, name in enumerate(names):
    wb = Workbook()
    ins = wb.active
    ins.title = "How to review 说明"
    for r, t in enumerate([
        f"PAPP conversation test — 4 short audio clips — for {name}",
        "",
        "Thank you again! This is different from the phrase list: here I need your EARS. / 再次感谢！这次和短句表不同：需要您听录音。",
        "",
        "Each clip is 3–5 seconds of someone speaking Chinese (files PAPP-z16-1.m4a … z16-4.m4a).",
        "每段录音 3–5 秒（文件 PAPP-z16-1.m4a … z16-4.m4a）。",
        "",
        "For each clip, on the next sheet:",
        "  1. Listen (twice is fine) and WRITE WHAT YOU HEAR in Chinese in column D — exactly, even if the computer column is wrong.",
        "     听录音（可以听两遍），在 D 栏写下您听到的中文原话——请照实写，即使电脑那一栏是错的。",
        "  2. Column E shows what my translator understood in English. Is the MEANING right? Choose OK or FIX in column F.",
        "     E 栏是我的翻译器给出的英文意思。意思对吗？在 F 栏选择 OK 或 FIX。",
        "  3. If FIX, explain briefly in column G (English or Chinese is fine).  如果选 FIX，请在 G 栏简单说明（中英文都可以）。",
        "",
        "Column C is what the computer HEARD — it can be wrong (板/版, 买/卖 …). That is exactly what I am testing.",
        "C 栏是电脑“听到”的文字——可能有错（板/版、买/卖……），这正是我要测试的。",
        "",
        "Time: about 10 minutes. Please send the file back on WeChat when done. 大约 10 分钟，完成后请在微信发回。",
    ], start=1):
        ins.cell(row=r, column=1, value=t).font = Font(bold=(r == 1), size=14 if r == 1 else 11)
    ins.column_dimensions["A"].width = 130
    ws = wb.create_sheet("Clips 录音")
    hdr = ["#", "Audio file 录音", "Computer heard (may be wrong) 电脑听到的", "What YOU hear 您听到的原话 ✍",
           "Translator meaning (EN) 翻译器的意思", "Meaning OK? 意思对吗", "Comment 备注"]
    for c, h in enumerate(hdr, 1):
        x = ws.cell(row=1, column=c, value=h); x.font, x.fill = head, fill
        x.alignment = Alignment(wrap_text=True, vertical="center")
    dv = DataValidation(type="list", formula1='"OK,FIX"', allow_blank=True)
    ws.add_data_validation(dv)
    for i, (cid, heard, en) in enumerate(CLIPS, 2):
        for c, v in enumerate([i - 1, f"PAPP-{cid}.m4a", heard, "", en, "", ""], 1):
            x = ws.cell(row=i, column=c, value=v)
            x.alignment = Alignment(wrap_text=True, vertical="top")
            x.border = Border(top=thin, bottom=thin, left=thin, right=thin)
            if c in (4, 6, 7):
                x.fill = ans
        dv.add(f"F{i}")
        ws.row_dimensions[i].height = 48
    for col, w in zip("ABCDEFG", (4, 16, 34, 34, 40, 12, 30)):
        ws.column_dimensions[col].width = w
    ws.freeze_panes = "A2"
    fn = os.path.join(out_dir, f"PAPP-dialog-review-{chr(65 + idx)}-{name}.xlsx")
    wb.save(fn)
    print(fn)
