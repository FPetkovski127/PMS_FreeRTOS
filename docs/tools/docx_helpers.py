"""Small helper layer on top of python-docx used by build_report1.py and build_report2.py.

Features: numbered headings, inline markup (**bold**, `code`, [[italic]]), tables with shaded header,
figures with numbered captions, code blocks, call-out boxes, equations, TOC field, page numbers.
"""
import os
import re
from docx import Document
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

HERE = os.path.dirname(os.path.abspath(__file__))
FIG_DIR = os.path.join(HERE, "..", "figures")

DARK = RGBColor(0x1F, 0x38, 0x64)
GREY = RGBColor(0x6B, 0x72, 0x80)
TEXT_W_CM = 17.0

_TOKEN = re.compile(r"(\*\*[^*]+\*\*|`[^`]+`|\[\[[^\]]+\]\])")


def _shade(el_pr, fill):
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), fill)
    el_pr.append(shd)


def _field(run, instr, placeholder=""):
    for typ in ("begin", None, "separate", "text", "end"):
        if typ is None:
            it = OxmlElement("w:instrText")
            it.set(qn("xml:space"), "preserve")
            it.text = instr
            run._r.append(it)
        elif typ == "text":
            t = OxmlElement("w:t")
            t.text = placeholder
            run._r.append(t)
        else:
            fc = OxmlElement("w:fldChar")
            fc.set(qn("w:fldCharType"), typ)
            run._r.append(fc)


def _set_font(style, name, size, bold=None, color=None):
    style.font.name = name
    style.font.size = Pt(size)
    rpr = style.element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.append(rfonts)
    for a in ("w:ascii", "w:hAnsi", "w:eastAsia", "w:cs"):
        rfonts.set(qn(a), name)
    for a in ("w:asciiTheme", "w:hAnsiTheme", "w:eastAsiaTheme", "w:cstheme"):   # theme fonts would override the name
        if rfonts.get(qn(a)) is not None:
            del rfonts.attrib[qn(a)]
    if bold is not None:
        style.font.bold = bold
    if color is not None:
        style.font.color.rgb = color


class Report:
    def __init__(self, short_title):
        self.doc = Document()
        self.short_title = short_title
        self.h = [0, 0, 0]
        self.fig_n = 0
        self.tab_n = 0
        self._setup()

    # ------------------------------------------------------------------ setup
    def _setup(self):
        d = self.doc
        sec = d.sections[0]
        sec.page_width, sec.page_height = Cm(21.0), Cm(29.7)
        sec.left_margin = sec.right_margin = Cm(2.0)
        sec.top_margin, sec.bottom_margin = Cm(2.0), Cm(2.0)

        st = d.styles
        _set_font(st["Normal"], "Calibri", 10.5)
        st["Normal"].paragraph_format.space_after = Pt(6)
        st["Normal"].paragraph_format.line_spacing = 1.1
        _set_font(st["Heading 1"], "Calibri", 17, True, DARK)
        _set_font(st["Heading 2"], "Calibri", 13.5, True, DARK)
        _set_font(st["Heading 3"], "Calibri", 11.5, True, RGBColor(0x2F, 0x54, 0x96))
        st["Heading 1"].paragraph_format.space_before = Pt(18)
        st["Heading 1"].paragraph_format.space_after = Pt(8)
        st["Heading 2"].paragraph_format.space_before = Pt(12)
        st["Heading 2"].paragraph_format.space_after = Pt(5)
        st["Heading 3"].paragraph_format.space_before = Pt(8)
        st["Heading 3"].paragraph_format.space_after = Pt(3)
        _set_font(st["Caption"], "Calibri", 9, False, RGBColor(0x37, 0x41, 0x51))
        st["Caption"].font.italic = True
        for name in ("List Bullet",):
            _set_font(st[name], "Calibri", 10.5)
        from docx.enum.style import WD_STYLE_TYPE
        code = st.add_style("CodeBlock", WD_STYLE_TYPE.PARAGRAPH)
        code.base_style = st["Normal"]
        _set_font(code, "Consolas", 8.5)
        code.paragraph_format.space_after = Pt(0)
        code.paragraph_format.space_before = Pt(0)
        code.paragraph_format.left_indent = Cm(0.3)
        code.paragraph_format.line_spacing = 1.0

        # header / footer
        hp = sec.header.paragraphs[0]
        hp.alignment = WD_ALIGN_PARAGRAPH.RIGHT
        r = hp.add_run(self.short_title)
        r.font.size = Pt(8.5)
        r.font.color.rgb = GREY
        fp = sec.footer.paragraphs[0]
        fp.alignment = WD_ALIGN_PARAGRAPH.CENTER
        r = fp.add_run("Page ")
        r.font.size = Pt(9)
        r2 = fp.add_run()
        r2.font.size = Pt(9)
        _field(r2, "PAGE", "1")
        # ask Word to refresh fields (TOC) when the file is opened
        uf = OxmlElement("w:updateFields")
        uf.set(qn("w:val"), "true")
        d.settings.element.append(uf)

    # ---------------------------------------------------------------- text
    def runs(self, par, text, size=None, bold=False, italic=False, color=None):
        for part in _TOKEN.split(text):
            if not part:
                continue
            if part.startswith("**"):
                r = par.add_run(part[2:-2])
                r.bold = True
            elif part.startswith("`"):
                r = par.add_run(part[1:-1])
                r.font.name = "Consolas"
                r._r.get_or_add_rPr().rFonts.set(qn("w:eastAsia"), "Consolas")
                r.font.size = Pt((size or 10.5) - 1)
                r.font.color.rgb = RGBColor(0x9A, 0x34, 0x12)
            elif part.startswith("[["):
                r = par.add_run(part[2:-2])
                r.italic = True
            else:
                r = par.add_run(part)
                if bold:
                    r.bold = True
            if size and not part.startswith("`"):
                r.font.size = Pt(size)
            if italic:
                r.italic = True
            if color is not None:
                r.font.color.rgb = color
        return par

    def p(self, text, align=None, space_after=None, size=None, italic=False, color=None, keep=False):
        par = self.doc.add_paragraph()
        self.runs(par, text, size=size, italic=italic, color=color)
        if align == "center":
            par.alignment = WD_ALIGN_PARAGRAPH.CENTER
        elif align == "justify":
            par.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
        if space_after is not None:
            par.paragraph_format.space_after = Pt(space_after)
        if keep:
            par.paragraph_format.keep_with_next = True
        return par

    def bullets(self, items):
        for it in items:
            par = self.doc.add_paragraph(style="List Bullet")
            self.runs(par, it)
            par.paragraph_format.space_after = Pt(2)

    def numbered(self, items):
        for i, it in enumerate(items, 1):
            par = self.doc.add_paragraph()
            par.paragraph_format.left_indent = Cm(0.9)
            par.paragraph_format.first_line_indent = Cm(-0.6)
            par.paragraph_format.space_after = Pt(2)
            self.runs(par, f"{i}.  " + it)

    def callout(self, text, kind="note"):
        fills = {"note": ("DBEAFE", "NOTE  "), "warn": ("FEF3C7", "ATTENTION  "), "todo": ("FEE2E2", "TO BE COMPLETED  "),
                 "check": ("EDE9FE", "TO CONFIRM  ")}
        fill, label = fills[kind]
        par = self.doc.add_paragraph()
        par.paragraph_format.left_indent = Cm(0.3)
        par.paragraph_format.right_indent = Cm(0.3)
        par.paragraph_format.space_before = Pt(4)
        par.paragraph_format.space_after = Pt(8)
        _shade(par._p.get_or_add_pPr(), fill)
        r = par.add_run(label)
        r.bold = True
        r.font.size = Pt(9.5)
        self.runs(par, text, size=9.5)

    def equation(self, text, number=None):
        par = self.doc.add_paragraph()
        par.alignment = WD_ALIGN_PARAGRAPH.CENTER
        par.paragraph_format.space_before = Pt(3)
        par.paragraph_format.space_after = Pt(6)
        r = par.add_run(text + (f"        ({number})" if number else ""))
        r.font.name = "Cambria Math"
        r.font.size = Pt(11)
        r.italic = True

    def code(self, text):
        lines = text.strip("\n").split("\n")
        for i, ln in enumerate(lines):
            par = self.doc.add_paragraph(style="CodeBlock")
            _shade(par._p.get_or_add_pPr(), "F3F4F6")
            par.add_run(ln if ln else " ")
            if i < len(lines) - 1:
                par.paragraph_format.keep_with_next = True
        self.doc.add_paragraph().paragraph_format.space_after = Pt(2)

    def page_break(self):
        self.doc.add_paragraph().add_run().add_break(WD_BREAK.PAGE)

    # ------------------------------------------------------------- headings
    def heading(self, text, level, numbered=True):
        if numbered:
            self.h[level - 1] += 1
            for i in range(level, 3):
                self.h[i] = 0
            num = ".".join(str(n) for n in self.h[:level])
            text = f"{num}  {text}"
        self.doc.add_heading(text, level=level)

    def h1(self, t, numbered=True):
        self.heading(t, 1, numbered)

    def h2(self, t):
        self.heading(t, 2)

    def h3(self, t):
        self.heading(t, 3)

    # --------------------------------------------------------------- tables
    def table(self, headers, rows, widths=None, caption=None, font=9, align_center_cols=()):
        if caption:
            self.tab_n += 1
            cp = self.doc.add_paragraph(style="Caption")
            cp.paragraph_format.keep_with_next = True
            cp.paragraph_format.space_before = Pt(6)
            cp.add_run(f"Table {self.tab_n}. {caption}")
        t = self.doc.add_table(rows=1, cols=len(headers))
        t.style = "Table Grid"
        t.alignment = WD_TABLE_ALIGNMENT.CENTER
        t.autofit = False
        if widths is None:
            widths = [TEXT_W_CM / len(headers)] * len(headers)
        else:
            s = sum(widths)
            widths = [w * TEXT_W_CM / s for w in widths]
        for i, h in enumerate(headers):
            c = t.rows[0].cells[i]
            c.width = Cm(widths[i])
            _shade(c._tc.get_or_add_tcPr(), "1F3864")
            par = c.paragraphs[0]
            par.paragraph_format.space_after = Pt(1)
            r = par.add_run(h)
            r.bold = True
            r.font.size = Pt(font)
            r.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)
        # repeat header row on every page
        trpr = t.rows[0]._tr.get_or_add_trPr()
        th = OxmlElement("w:tblHeader")
        th.set(qn("w:val"), "true")
        trpr.append(th)
        for ri, row in enumerate(rows):
            cells = t.add_row().cells
            for i, val in enumerate(row):
                cells[i].width = Cm(widths[i])
                par = cells[i].paragraphs[0]
                par.paragraph_format.space_after = Pt(1)
                par.paragraph_format.line_spacing = 1.0
                if i in align_center_cols:
                    par.alignment = WD_ALIGN_PARAGRAPH.CENTER
                self.runs(par, str(val), size=font)
                if ri % 2 == 1:
                    _shade(cells[i]._tc.get_or_add_tcPr(), "F3F6FB")
        self.doc.add_paragraph().paragraph_format.space_after = Pt(2)

    # -------------------------------------------------------------- figures
    def figure(self, name, caption, width_cm=TEXT_W_CM):
        path = os.path.join(FIG_DIR, name + ".png")
        if not os.path.exists(path):
            self.callout(f"figure file missing: {name}.png", "todo")
            return
        self.fig_n += 1
        par = self.doc.add_paragraph()
        par.alignment = WD_ALIGN_PARAGRAPH.CENTER
        par.paragraph_format.keep_with_next = True
        par.paragraph_format.space_before = Pt(6)
        par.paragraph_format.space_after = Pt(2)
        par.add_run().add_picture(path, width=Cm(width_cm))
        cp = self.doc.add_paragraph(style="Caption")
        cp.alignment = WD_ALIGN_PARAGRAPH.CENTER
        cp.paragraph_format.space_after = Pt(10)
        cp.add_run(f"Figure {self.fig_n}. {caption}")
        return self.fig_n

    # ------------------------------------------------------ front matter
    def title_page(self, title, subtitle, lines, abstract=None):
        for _ in range(5):
            self.doc.add_paragraph()
        par = self.doc.add_paragraph()
        par.alignment = WD_ALIGN_PARAGRAPH.CENTER
        r = par.add_run(title)
        r.font.size = Pt(26)
        r.bold = True
        r.font.color.rgb = DARK
        par = self.doc.add_paragraph()
        par.alignment = WD_ALIGN_PARAGRAPH.CENTER
        r = par.add_run(subtitle)
        r.font.size = Pt(14)
        r.font.color.rgb = RGBColor(0x2F, 0x54, 0x96)
        for _ in range(3):
            self.doc.add_paragraph()
        for ln in lines:
            par = self.doc.add_paragraph()
            par.alignment = WD_ALIGN_PARAGRAPH.CENTER
            par.paragraph_format.space_after = Pt(2)
            self.runs(par, ln, size=11)
        if abstract:
            self.doc.add_paragraph()
            self.doc.add_paragraph()
            self.callout(abstract, "note")
        self.page_break()

    def toc(self, title="Contents"):
        par = self.doc.add_paragraph()
        r = par.add_run(title)
        r.bold = True
        r.font.size = Pt(17)
        r.font.color.rgb = DARK
        par = self.doc.add_paragraph()
        r = par.add_run()
        _field(r, 'TOC \\o "1-3" \\h \\z \\u', "Right-click here and choose 'Update field' to build the table of contents.")
        self.page_break()

    def save(self, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        self.doc.save(path)
        print("saved", path)
