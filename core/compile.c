//
// compile.c
// ast to bytecode
//
// (c) 2008 why the lucky stiff, the freelance professor
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "potion.h"
#include "internal.h"
#include "pn-ast.h"
#include "opcodes.h"

#define PN_ASM1(ins, a) ({ \
    *((*pos)++) = (u8)ins; \
    *((*pos)++) = (u8)a; \
  })

#define PN_ASM2(ins, a, b) ({ \
    *((*pos)++) = (u8)ins; \
    *((*pos)++) = (u8)a; \
    *((*pos)++) = (u8)b; \
  })

const char *potion_op_names[] = {
  "move", "loadk", "loadnil", "loadbool",
  "getlocal", "setlocal", "newtable", "gettable",
  "settable", "getpath", "setpath", "self",
  "bind", "test", "testset", "call",
  "tailcall", "return", "proto"
};

const u8 potion_op_args[] = {
  2, 2, 1, 1,
  2, 2, 1, 2,
  2, 2, 2, 1,
  2, 2, 2, 2,
  2, 1, 2
};

PN potion_proto_call(Potion *P, PN cl, PN self) {
  return potion_vm(P, self);
}

PN potion_proto_inspect(Potion *P, PN cl, PN self) {
  struct PNProto *t = (struct PNProto *)self;
  unsigned int num = 1;
  u8 *pos, *end, i = 0;
  printf("; function definition: %p ; %u bytes\n", t, PN_STR_LEN(t->asmb));
  printf("; %ld stacks\n", PN_INT(t->stack));
  PN_TUPLE_EACH(t->locals, i, v, {
    printf(".local \"");
    potion_send(v, PN_inspect);
    printf("\" ; %lu\n", i);
  });
  PN_TUPLE_EACH(t->values, i, v, {
    printf(".value ");
    potion_send(v, PN_inspect);
    printf(" ; %lu\n", i);
  });
  PN_TUPLE_EACH(t->protos, i, v, {
    potion_send(v, PN_inspect);
  });
  pos = (u8 *)PN_STR_PTR(t->asmb);
  end = (u8 *)PN_STR_PTR(t->asmb) + PN_STR_LEN(t->asmb);
  while (pos < end) {
    printf("[%u] %s", num, potion_op_names[pos[0]]);
    for (i = 0; i < potion_op_args[pos[0]]; i++)
      printf(" %u", (unsigned)pos[i+1]);
    pos += i + 1;
    printf("\n");
    num++;
  }
  printf("; function end\n");
  return PN_NIL;
}

void potion_source_asmb(Potion *P, struct PNProto *f, struct PNSource *t, u8 reg, u8 **pos) {
  if (reg >= PN_INT(f->stack))
    f->stack = PN_NUM(reg + 1);

  switch (t->part) {
    case AST_CODE:
    case AST_BLOCK:
    case AST_EXPR:
      PN_TUPLE_EACH(t->a[0], i, v, {
        potion_source_asmb(P, f, (struct PNSource *)v, reg, pos);
      });
      if (t->part != AST_EXPR)
        PN_ASM1(OP_RETURN, 0);
    break;

    case AST_PROTO: {
      PN block = potion_send(t->a[1], PN_compile, PN_NIL, t->a[0]);
      unsigned long num = PN_PUT(f->protos, block);
      PN_ASM2(OP_PROTO, reg, num);
    }
    break;

    case AST_VALUE: {
      unsigned long num = PN_PUT(f->values, t->a[0]);
      PN_ASM2(OP_LOADK, reg, num);
    }
    break;

    case AST_ASSIGN: {
      struct PNSource *lhs = (struct PNSource *)t->a[0];
      unsigned long num = PN_NONE;

      if (lhs->part == AST_MESSAGE || lhs->part == AST_QUERY)
        num = PN_PUT(f->locals, lhs->a[0]);
      else if (lhs->part == AST_PATH || lhs->part == AST_PATHQ)
        num = PN_PUT(f->values, lhs->a[0]);

      PN_TUPLE_EACH(t->a[1], i, v, {
        potion_source_asmb(P, f, (struct PNSource *)v, reg, pos);
      });

      if (lhs->part == AST_MESSAGE || lhs->part == AST_QUERY) {
        if (lhs->part == AST_QUERY) {
          PN_ASM2(OP_GETLOCAL, reg, num);
          PN_ASM2(OP_TEST, reg, 1); 
        }
        PN_ASM2(OP_SETLOCAL, reg, num);
      } else if (lhs->part == AST_PATH || lhs->part == AST_PATHQ) {
        if (lhs->part == AST_PATHQ) {
          PN_ASM2(OP_GETPATH, reg, num);
          PN_ASM2(OP_TEST, num, 1);
        }
        PN_ASM2(OP_SETPATH, reg, num);
      }
    }
    break;

    case AST_MESSAGE:
    case AST_QUERY: {
      u8 breg = reg;
      unsigned long num = PN_GET(f->locals, t->a[0]);
      if (num == PN_NONE) {
        if (t->a[1] != PN_NIL)
          potion_source_asmb(P, f, (struct PNSource *)t->a[1], ++breg, pos);
        num = PN_PUT(f->values, t->a[0]);
        PN_ASM2(OP_LOADK, ++breg, num);
        PN_ASM2(OP_BIND, breg, reg);
        if (t->part == AST_MESSAGE) {
          PN_ASM2(OP_CALL, reg, breg);
        } else
          PN_ASM2(OP_TEST, reg, breg);
      } else {
        if (t->part == AST_QUERY) {
          PN_ASM2(OP_GETLOCAL, reg, num);
          PN_ASM2(OP_TEST, reg, reg);
        } else if (t->a[1] != PN_NIL) {
          potion_source_asmb(P, f, (struct PNSource *)t->a[1], breg, pos);
          PN_ASM2(OP_GETLOCAL, ++breg, num);
          PN_ASM2(OP_CALL, reg, breg);
        } else
          PN_ASM2(OP_GETLOCAL, reg, num);
      }
    }
    break;

    case AST_PATH:
    case AST_PATHQ: {
      unsigned long num = PN_PUT(f->values, t->a[0]);
      PN_ASM2(OP_GETPATH, 0, num);
      if (t->part == AST_PATHQ)
        PN_ASM2(OP_TEST, num, 1);
    }
    break;

    case AST_TABLE:
      PN_ASM1(OP_NEWTABLE, 0);
      PN_TUPLE_EACH(t->a[0], i, v, {
        potion_source_asmb(P, f, (struct PNSource *)v, reg, pos);
      });
    break;
  }
}

PN potion_source_compile(Potion *P, PN cl, PN self, PN source, PN sig) {
  struct PNProto *f;
  struct PNSource *t = (struct PNSource *)self;
  u8 *start, *pos;
  switch (t->part) {
    case AST_CODE:
    case AST_BLOCK: break;
    default: return PN_NIL; // TODO: error
  }

  f = PN_OBJ_ALLOC(struct PNProto, PN_TPROTO, 0);
  f->source = source;
  f->sig = (sig == PN_NIL ? PN_EMPTY : sig);
  f->stack = PN_NUM(1);
  f->protos = f->locals = f->values = PN_EMPTY;
  f->asmb = potion_bytes(P, 8192);

  start = pos = (u8 *)PN_STR_PTR(f->asmb);
  potion_source_asmb(P, f, t, 0, &pos);
  // TODO: byte strings should be more flexible than this
  PN_STR_LEN(f->asmb) = pos - start;
  return (PN)f;
}

#define READ_PN(pn, ptr) ({PN rpn = *(PN *)ptr; ptr += pn; rpn;})
#define READ_CONST(pn, ptr) ({ \
    PN val = READ_PN(pn, ptr); \
    if (potion_is_ref(val)) { \
      size_t len = val >> 3; \
      val = potion_str2(P, (char *)ptr, len); \
      ptr += len; \
    } \
    val; \
  })

// [PN] count | [PN] [payload] | [PN] | ...
#define READ_VALUES(pn, ptr) ({ \
    long i = 0, count = PN_INT(READ_PN(pn, ptr)); \
    PN tup = potion_tuple_with_size(P, (unsigned long)count); \
    for (; i < count; i++) \
      PN_TUPLE_AT(tup, i) = READ_CONST(pn, ptr); \
    tup; \
  })
#define READ_OBJ(pn, ptr, vals) ({ \
    PN val = READ_PN(pn, ptr); \
    if (potion_is_ref(val)) \
      val = PN_TUPLE_AT(vals, val >> 3); \
    val; \
  })

// TODO: load from a stream
PN potion_source_load(Potion *P, PN cl, PN buf) {
  u8 *ptr, *end;
  struct PNProto *f;
  struct PNBHeader *h = (struct PNBHeader *)PN_STR_PTR(buf);
  if ((size_t)PN_STR_LEN(buf) <= sizeof(struct PNBHeader) || 
      strncmp((char *)h->sig, POTION_SIG, 4) != 0)
    return PN_NONE;

  ptr = h->proto;
  end = (u8 *)PN_STR_PTR(buf) + PN_STR_LEN(buf);
  f = PN_OBJ_ALLOC(struct PNProto, PN_TPROTO, 0);
  f->values = READ_VALUES(h->pn, ptr);
  f->locals = READ_VALUES(h->pn, ptr);
  f->protos = READ_VALUES(h->pn, ptr);
  f->source = READ_OBJ(h->pn, ptr, f->values);
  f->sig = READ_OBJ(h->pn, ptr, f->values);
  f->stack = READ_OBJ(h->pn, ptr, f->values);
  // TODO: no need to memcpy the bytecode with flexible byte strings
  f->asmb = potion_bytes(P, end - ptr);
  PN_MEMCPY_N(PN_STR_PTR(f->asmb), ptr, u8, end - ptr);
  return (PN)f;
}

#define WRITE_VALUES(pn, ptr)
#define WRITE_OBJ(pn, ptr)

// TODO: dump to a stream
PN potion_source_dump(Potion *P, PN cl, PN proto) {
  u8 *ptr;
  PN pnb = potion_bytes(P, 8192);
  struct PNProto *f = (struct PNProto *)proto;
  struct PNBHeader h;
  PN_MEMCPY_N(h.sig, POTION_SIG, u8, 4);
  h.vmid = POTION_VMID;
  h.pn = (u8)sizeof(PN);

  ptr = (u8 *)PN_STR_PTR(pnb);
  PN_MEMCPY(ptr, &h, struct PNBHeader);
  ptr += sizeof(struct PNBHeader);
  WRITE_VALUES(f->values, ptr);
  WRITE_VALUES(f->locals, ptr);
  WRITE_VALUES(f->protos, ptr);
  WRITE_OBJ(f->source, ptr);
  WRITE_OBJ(f->sig, ptr);
  WRITE_OBJ(f->stack, ptr);
  PN_MEMCPY_N(ptr, PN_STR_PTR(f->asmb), u8, PN_STR_LEN(f->asmb));
  ptr += PN_STR_LEN(f->asmb);
  PN_STR_LEN(pnb) = (char *)ptr - PN_STR_PTR(pnb);
  return pnb;
}

void potion_compiler_init(Potion *P) {
  PN pro_vt = PN_VTABLE(PN_TPROTO);
  potion_method(pro_vt, "inspect", potion_proto_inspect, 0);
  potion_method(pro_vt, "call", potion_proto_call, 0);
}
