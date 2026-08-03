import re
from coc_string import unescape_operator

class data_value:
    def __init__(self):
        self.value = None
        self.depend = None

class object_block:
    def __init__(self):
        self.type = None
        self.name = None
        self.attr = {}
        self.data = {}
        self.data_ordered = []

class coc_parser:
    """Parser for Berry"""

    def __init__(self, text):
        """Parse text file"""
        self.objects = []
        self.strtab = set()
        self.strtab_weak = set()
        self.strtab_long = set()
        self.bintab = set()
        self.text = text
        self.parsers = {
            "@const_object_info_begin": self.parse_object,
            "be_const_str_": self.parse_string,
            "be_const_bytes_instance(": self.parse_bin,
            "be_const_key(": self.parse_string,
            "be_nested_str(": self.parse_string,
            "be_const_key_weak(": self.parse_string_weak,
            "be_nested_str_weak(": self.parse_string_weak,
            "be_nested_str_long(": self.parse_string_long,
            "be_str_weak(": self.parse_string_weak,
        }

        while len(self.text) > 0:
            pattern = "|".join(self.parsers.keys())
            pattern = re.sub("\\(", "\\(", pattern)
            r = re.search(pattern, self.text)
            if not r: break

            self.text = self.text[r.end(0):]
            func = self.parsers[r[0]]
            func()


    def skip_space(self):
        r = re.match(r"\s+", self.text)
        if r:
            self.text = self.text[r.end(0):]

    def parse_char_base(self, c, necessary):
        res = self.text[0] == c
        if not res and necessary:   print(self.text); raise "error"
        if res: self.text = self.text[1:]
        return res

    def parse_char(self, c, necessary = False):
        self.skip_space()
        return self.parse_char_base(c, necessary)

    def skip_char(self, c):
        self.parse_char(c, True)

    def parse_char_continue(self, c, necessary = False):
        ch = self.text[0]
        while ch == ' ' or ch == "\t":  self.text = self.text[1:]
        return self.parse_char_base(c, necessary)

    def parse_word(self):
        self.skip_space()
        r = re.match(r"\w+", self.text)
        if not r: return None
        self.text = self.text[r.end(0):]
        return r[0]

    def parse_tocomma(self):
        self.skip_space()
        r = re.match(r"[^,\s]*", self.text)
        self.text = self.text[r.end(0):]
        return r[0]

    def parse_value(self):
        self.skip_space()
        r = re.match(r"(\S+\(.*?\))|([^,\s]*)", self.text)
        self.text = self.text[r.end(0):]
        return r[0]

    def parse_tonewline(self):
        self.skip_space()
        r = re.match(r"[^\r\n]*", self.text)
        self.text = self.text[r.end(0):]
        return r[0]

    def parse_object(self):
        self.text = re.sub(r"\s+//.*?$", "", self.text, flags=re.MULTILINE)
        while True:
            obj = self.parse_block()
            self.objects.append(obj)
            if self.parse_char("@"): break

        end_text = "const_object_info_end"
        if not str.startswith(self.text, end_text): raise "error"
        self.text = self.text[len(end_text):]

    def parse_string(self):
        if not self.text[0].isalnum() and self.text[0] != '_': return
        ident = self.parse_word()
        if not ident: return
        literal = unescape_operator(ident)
        if not literal in self.strtab:
            self.strtab.add(literal)

    def parse_string_weak(self):
        if not self.text[0].isalnum() and self.text[0] != '_': return
        ident = self.parse_word()
        if not ident: return
        literal = unescape_operator(ident)
        if not literal in self.strtab:
            self.strtab_weak.add(literal)

    def parse_string_long(self):
        if not self.text[0].isalnum() and self.text[0] != '_': return
        ident = self.parse_word()
        if not ident: return
        literal = unescape_operator(ident)
        if not literal in self.strtab:
            self.strtab_long.add(literal)

    def parse_bin(self):
        ident = self.parse_word()
        if not ident: return
        if not re.fullmatch(r"[0-9A-Za-z]*", ident): return
        if not ident in self.bintab:
            self.bintab.add(ident)

    def parse_block(self):
        obj = object_block()
        obj.type = self.parse_word()
        obj.name = self.parse_word()
        self.parse_attr(obj)
        self.parse_body(obj)
        return obj

    def parse_attr(self, obj):
        self.skip_char("(")
        self.parse_attr_pair(obj)
        while self.parse_char(","):
            self.parse_attr_pair(obj)
        self.skip_char(")")

    def parse_attr_pair(self, obj):
        key = self.parse_word()
        self.skip_char(":")
        value = self.parse_word()
        obj.attr[key] = value

    def parse_body(self, obj):
        self.skip_char("{")
        if not self.parse_char("}"):
            while True:
                self.parse_body_item(obj)
                if self.parse_char("}"): break

    def parse_body_item(self, obj):
        value = data_value()
        key = self.parse_tocomma()
        self.parse_char_continue(",", True)
        value.value = self.parse_value()
        if self.parse_char_continue(","):
            value.depend = self.parse_tonewline()
        obj.data[key] = value
        obj.data_ordered.append(key)
