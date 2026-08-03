import copy
from coc_string import *

def list_resize_entry(l, newsize, filling=None):
    if newsize > len(l):
        l.extend([entry() for x in range(len(l), newsize)])
    else:
        del l[newsize:]

class entry:
    def __init__(self):
        self.key = ""
        self.value = ""
        self.next = hash_map.NODE_EMPTY

    def __repr__(self):
        return f"<entry object; key='{self.key}', value='{self.value}', next={self.next}>"

class hash_map:
    NODE_EMPTY = -2
    NODE_NULL = -1

    def __init__(self, map):
        self.count = 0
        self.lastfree = 0
        self.bucket = []

        self.resize(2)
        var_count = 0
        for (key, value) in map.items():
            if value == "var":
                map[key] = var_count
                var_count += 1

        for key in sorted(map.keys()):
            self.insert(key, map[key])

    def __repr__(self):
        return f"<hash_map object; count={self.count}, bucket={self.bucket}, lastfree={self.lastfree}>"

    def is_empty(self, ent):
        return ent.next == hash_map.NODE_EMPTY

    def resize(self, size):
        bucket = copy.deepcopy(self.bucket)
        list_resize_entry(self.bucket, size)
        for i in range(size):
            self.bucket[i].next = hash_map.NODE_EMPTY
        self.lastfree = size - 1
        for slot in bucket:
            if slot.next != hash_map.NODE_EMPTY:
                self.insert_p(slot.key, slot.value)

    def findprev(self, prev, slot):
        while True:
            next = prev.next
            if next == hash_map.NODE_NULL or self.bucket[next] == slot: break
            prev = self.bucket[next]
        if next == hash_map.NODE_NULL: return None
        return prev

    def nextfree(self):
        while self.lastfree >= 0:
            if self.bucket[self.lastfree].next == hash_map.NODE_EMPTY:
                return self.lastfree
            self.lastfree -= 1
        return -1

    def find(self, key):
        hash = hashcode(key)
        null = entry()
        slot = self.bucket[hash % len(self.bucket)]
        if slot.next == hash_map.NODE_EMPTY:
            return null
        while slot.key != key:
            if slot.next == hash_map.NODE_NULL: return null
            slot = self.bucket[slot.next]
        return slot

    def insert_p(self, key, value):
        slot = self.bucket[hashcode(key) % len(self.bucket)]
        if slot.next == hash_map.NODE_EMPTY:
            slot.next = hash_map.NODE_NULL
        else:
            newidx = self.nextfree()
            mainslot = self.bucket[hashcode(slot.key) % len(self.bucket)]
            newslot = self.bucket[newidx]
            if mainslot == slot:
                newslot.next = mainslot.next
                mainslot.next = newidx
                slot = newslot
            else:
                prev = self.findprev(mainslot, slot)
                prev.next = newidx
                newslot.key = slot.key
                newslot.value = slot.value
                newslot.next = slot.next
                slot.next = hash_map.NODE_NULL
        slot.key = key
        slot.value = value

    def insert(self, key, value):
        slot = self.find(key)
        if slot.next == hash_map.NODE_EMPTY:
            if self.count >= len(self.bucket):
                self.resize(len(self.bucket) * 2)
            self.insert_p(key, value)
            self.count += 1

    def entry_modify(self, ent):
        ent.key = escape_operator(ent.key)
        if isinstance(ent.value, int):
            ent.value = "be_const_var(" + str(ent.value) + ")"
        else:
            ent.value = "be_const_" + ent.value
        return ent

    def entry_list(self):
        l = []

        self.resize(self.count)
        for it in self.bucket:
            l.append(self.entry_modify(it))
        return l

    def var_count(self):
        count = 0

        self.resize(self.count)
        for it in self.bucket:
            if isinstance(it.value, int): count += 1
        return count

if __name__ == '__main__':
    m = hash_map({})
    print(m)
    m.insert("foo","bar")
    print(m)
    m.insert("foo2","bar")
    print(m)
    m.insert(".","3")
    print(m)
    m.insert("foo","bar2")
    print(m)
    m.insert("+","bar2")
    print(m)
    m.insert("a","var")
    m.insert("b","var")
    m.insert("c","var")
    print()
    print(m)
    print(f"var_count={m.var_count()}")
    print(f"entry_list={m.entry_list()}")