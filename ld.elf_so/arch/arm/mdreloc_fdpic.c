/*	$NetBSD: mdreloc.c,v 1.48 2024/08/03 21:59:58 riastradh Exp $	*/

#include "../../include/NetBSD/cdefs.h"
#ifndef lint
__RCSID("$NetBSD: mdreloc.c,v 1.48 2024/08/03 21:59:58 riastradh Exp $");
#endif /* not lint */

/*
 * Arm (32-bit) ELF relocations.
 *
 * Reference:
 *
 *	[AAELF32] ELF for the Arm Architecture, 2022Q3.  Arm Ltd.
 *	https://github.com/ARM-software/abi-aa/blob/2982a9f3b512a5bfdc9e3fea5d3b298f9165c36b/aaelf32/aaelf32.rst
 *
 *  ARM FDPIC ABI v1.0, Michael Guene
 *  https://github.com/mickael-guene/fdpic_doc/blob/master/abi.txt
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>

#include "../../debug.h"
#include "../../rtld.h"


struct elf_fdpic_desc {
	Elf_Addr f;
	Elf_Addr got;
};

void _rtld_bind_start(void);
void _rtld_relocate_nonplt_self(Elf_Dyn *, const struct elf_fdpic_loadmap *);
struct elf_fdpic_desc *_rtld_bind(const void *, Elf_Word);

void
_rtld_setup_pltgot(const Obj_Entry *obj)
{
	obj->pltgot[0] = ((struct elf_fdpic_desc *)&_rtld_bind_start)->f;
	obj->pltgot[1] = ((struct elf_fdpic_desc *)&_rtld_bind_start)->got;
	/* TODO: set pltgot to link map */
}

void
_rtld_relocate_nonplt_self(Elf_Dyn *dynp, const struct elf_fdpic_loadmap *loadmap)
{
	const Elf_Rel *rel = 0, *rellim;
	Elf_Addr relsz = 0;
	Elf_Addr *where;
	const Elf_Sym *symtab = NULL;

	for (; dynp->d_tag != DT_NULL; dynp++) {
		switch (dynp->d_tag) {
		case DT_REL:
			rel = (const Elf_Rel *)rtld_relocate(loadmap, dynp->d_un.d_ptr);
			break;
		case DT_RELSZ:
			relsz = dynp->d_un.d_val;
			break;
		case DT_SYMTAB:
			symtab = (const Elf_Sym *)rtld_relocate(loadmap, dynp->d_un.d_ptr);
			break;
		}
	}

	rellim = (const Elf_Rel *)((const uint8_t *)rel + relsz);
	for (; rel < rellim; rel++) {
		switch(ELF_R_TYPE(rel->r_info)) {
			case R_TYPE(RELATIVE):
				where = (Elf_Addr *)rtld_relocate(loadmap, rel->r_offset);
				*where = (Elf_Addr)rtld_relocate(loadmap, *where);
				break;
			case R_TYPE(FUNCDESC_VALUE):
				register void *r9 asm("r9");
				where = (Elf_Addr *)rtld_relocate(loadmap, rel->r_offset);
				if (symtab[ELF32_R_SYM(rel->r_info)].st_info == STT_SECTION) {
					where[0] += (Elf_Addr)rtld_relocate(loadmap, symtab[ELF32_R_SYM(rel->r_info)].st_value);
				} else {
					where[0] = (Elf_Addr)rtld_relocate(loadmap, symtab[ELF32_R_SYM(rel->r_info)].st_value);
				}
				where[1] = (Elf_Addr)r9;
				break;
			case R_TYPE(FUNCDESC):
				where = (Elf_Addr *)rtld_relocate(loadmap, rel->r_offset);
				*where = (Elf_Addr)r9;

				struct elf_fdpic_desc *dsc = r9;

				dsc->f = (Elf_Addr)rtld_relocate(loadmap, symtab[ELF32_R_SYM(rel->r_info)].st_value);
				dsc->got = (Elf_Addr)r9;
				break;
			case R_TYPE(NONE):
			case R_TYPE(TLS_DTPMOD32):
				break;
			default:
				abort();
				break;
		}
	}
}

/*
 * It is possible for the compiler to emit relocations for unaligned data.
 * We handle this situation with these inlines.
 */
#define	RELOC_ALIGNED_P(x) \
	(((uintptr_t)(x) & (sizeof(void *) - 1)) == 0)

static inline Elf_Addr
load_ptr(void *where)
{
	Elf_Addr res;

	memcpy(&res, where, sizeof(res));

	return (res);
}

static inline void
store_ptr(void *where, Elf_Addr val)
{

	memcpy(where, &val, sizeof(val));
}

int
_rtld_relocate_nonplt_objects(Obj_Entry *obj)
{
	const Elf_Rel *rel;
	const Elf_Sym *def = NULL;
	const Obj_Entry *defobj = NULL;
	unsigned long last_symnum = ULONG_MAX;
	struct elf_fdpic_desc *desc = NULL;
	size_t descsz = 0;

	for (rel = obj->rel; rel < obj->rellim; rel++) {
		if (ELF_R_TYPE(rel->r_info) == R_TYPE(FUNCDESC)) {
			descsz++;
		}
	}

	if (descsz != 0) {
		obj->descs = xmalloc(sizeof(void *) + descsz * sizeof(struct elf_fdpic_desc)); /* First word serves a pointer in list of descriptors. */
		if (obj->descs == NULL) {
			return -1;
		}
		((void **)obj->descs)[0] = NULL;
		desc = obj->descs + sizeof(void *);
	}


	for (rel = obj->rel; rel < obj->rellim; rel++) {
		Elf_Addr        *where;
		Elf_Addr         tmp;
		unsigned long	 symnum;

		where = (Elf_Addr *)rtld_relocate((struct elf_fdpic_loadmap *)&obj->loadmap, rel->r_offset);

		switch (ELF_R_TYPE(rel->r_info)) {
		case R_TYPE(PC24):	/* word32 S - P + A */
		case R_TYPE(ABS32):	/* word32 B + S + A */
		case R_TYPE(GLOB_DAT):	/* word32 B + S */
		case R_TYPE(TLS_DTPOFF32):
		case R_TYPE(TLS_DTPMOD32):
		case R_TYPE(TLS_TPOFF32):
		case R_TYPE(FUNCDESC):
		case R_TYPE(FUNCDESC_VALUE):
			symnum = ELF_R_SYM(rel->r_info);
			if (last_symnum != symnum) {
				last_symnum = symnum;
				def = _rtld_find_symdef(symnum, obj, &defobj,
				    false);
				if (def == NULL)
					return -1;
			}
			break;

		default:
			break;
		}

		switch (ELF_R_TYPE(rel->r_info)) {
		case R_TYPE(NONE):
			break;

#if 1 /* XXX should not occur */
		case R_TYPE(PC24): {	/* word32 S - P + A */
			Elf32_Sword addend;

			/*
			 * Extract addend and sign-extend if needed.
			 */
			addend = *where;
			if (addend & 0x00800000)
				addend |= 0xff000000;
			tmp = (Elf_Addr)rtld_relocate((struct elf_fdpic_loadmap *)&obj->loadmap, def->st_value
			    - (Elf_Addr)where + (addend << 2));
			if ((tmp & 0xfe000000) != 0xfe000000 &&
			    (tmp & 0xfe000000) != 0) {
				_rtld_error(
				"%s: R_ARM_PC24 relocation @ %p to %s failed "
				"(displacement %ld (%#lx) out of range)",
				    obj->path, where,
				    obj->strtab + obj->symtab[
				        ELF_R_SYM(rel->r_info)].st_name,
				    (long) tmp, (long) tmp);
				return -1;
			}
			tmp >>= 2;
			*where = (*where & 0xff000000) | (tmp & 0x00ffffff);
			rdbg(("PC24 %s in %s --> %p @ %p in %s",
			    obj->strtab + obj->symtab[ELF_R_SYM(rel->r_info)]
			    .st_name, obj->path, (void *)*where, where,
			    defobj->path));
			break;
		}
#endif

		case R_TYPE(ABS32):	/* word32 B + S + A */
		case R_TYPE(GLOB_DAT):	/* word32 B + S */
			if (__predict_true(RELOC_ALIGNED_P(where))) {
				tmp = *where + (Elf_Addr)rtld_relocate((struct elf_fdpic_loadmap *)&defobj->loadmap,
				    def->st_value);
				/* Set the Thumb bit, if needed.  */
				if (ELF_ST_TYPE(def->st_info) == STT_ARM_TFUNC)
				    tmp |= 1;
				*where = tmp;
			} else {
				tmp = load_ptr(where) +
				    (Elf_Addr)rtld_relocate((struct elf_fdpic_loadmap *)&defobj->loadmap,
				    def->st_value);
				/* Set the Thumb bit, if needed.  */
				if (ELF_ST_TYPE(def->st_info) == STT_ARM_TFUNC)
				    tmp |= 1;
				store_ptr(where, tmp);
			}
			rdbg(("ABS32/GLOB_DAT %s in %s --> %p @ %p in %s",
			    obj->strtab + obj->symtab[ELF_R_SYM(rel->r_info)]
			    .st_name, obj->path, (void *)tmp, where,
			    defobj->path));
			break;

		case R_TYPE(IRELATIVE):
			/* IFUNC relocations are handled in _rtld_call_ifunc */
			if (obj->ifunc_remaining_nonplt == 0)
				obj->ifunc_remaining_nonplt = obj->rellim - rel;
			/* FALL-THROUGH */

		case R_TYPE(RELATIVE):	/* word32 B + A */
			if (__predict_true(RELOC_ALIGNED_P(where))) {
				tmp = (Elf_Addr)rtld_relocate((struct elf_fdpic_loadmap *)&obj->loadmap, *where);
				*where = tmp;
			} else {
				tmp = (Elf_Addr)rtld_relocate((struct elf_fdpic_loadmap *)&obj->loadmap, load_ptr(where));
				store_ptr(where, tmp);
			}
			rdbg(("RELATIVE in %s --> %p", obj->path,
			    (void *)tmp));
			break;

		case R_TYPE(COPY):
			/*
			 * These are deferred until all other relocations have
			 * been done.  All we do here is make sure that the
			 * COPY relocation is not in a shared library.  They
			 * are allowed only in executable files.
			 */
			if (obj->isdynamic) {
				_rtld_error(
			"%s: Unexpected R_COPY relocation in shared library",
				    obj->path);
				return -1;
			}
			rdbg(("COPY (avoid in main)"));
			break;

		case R_TYPE(TLS_DTPOFF32):
			tmp = (Elf_Addr)(def->st_value);
			if (__predict_true(RELOC_ALIGNED_P(where)))
				*where = tmp;
			else
				store_ptr(where, tmp);

			rdbg(("TLS_DTPOFF32 %s in %s --> %p",
			    obj->strtab + obj->symtab[ELF_R_SYM(rel->r_info)]
			    .st_name, obj->path, (void *)tmp));

			break;
		case R_TYPE(TLS_DTPMOD32):
			tmp = (Elf_Addr)(defobj->tlsindex);
			if (__predict_true(RELOC_ALIGNED_P(where)))
				*where = tmp;
			else
				store_ptr(where, tmp);

			rdbg(("TLS_DTPMOD32 %s in %s --> %p",
			    obj->strtab + obj->symtab[ELF_R_SYM(rel->r_info)]
			    .st_name, obj->path, (void *)tmp));

			break;

		case R_TYPE(TLS_TPOFF32):
			if (!defobj->tls_static &&
			    _rtld_tls_offset_allocate(__UNCONST(defobj)))
				return -1;

			if (__predict_true(RELOC_ALIGNED_P(where)))
				tmp = *where;
			else
				tmp = load_ptr(where);
			tmp += (Elf_Addr)def->st_value + defobj->tlsoffset + sizeof(struct tls_tcb);
			if (__predict_true(RELOC_ALIGNED_P(where)))
				*where = tmp;
			else
				store_ptr(where, tmp);
			rdbg(("TLS_TPOFF32 %s in %s --> %p",
			    obj->strtab + obj->symtab[ELF_R_SYM(rel->r_info)]
			    .st_name, obj->path, (void *)tmp));
			break;

		case R_TYPE(FUNCDESC): {
			tmp = (Elf_Addr)rtld_relocate((struct elf_fdpic_loadmap *)&defobj->loadmap, def->st_value);

			if (tmp == (Elf_Addr)NULL) {
				*where = (Elf_Addr)NULL;
			} else {
				desc->f = tmp;
				desc->got = (Elf_Addr)defobj->pltgot;

				*where = (Elf_Addr)desc;
				desc++;

				dbg(("FUNCDESC %p %p", (void *)desc->f, (void *)def->st_value));
				dbg_rtld_dump_loadmap((struct elf_fdpic_loadmap *)&defobj->loadmap);
			}

			break;
		}
		case R_TYPE(FUNCDESC_VALUE):
			if (def->st_info == STT_SECTION) {
				where[0] += (Elf_Addr)rtld_relocate((struct elf_fdpic_loadmap *)&defobj->loadmap, def->st_value);
			} else {
				where[0] = (Elf_Addr)rtld_relocate((struct elf_fdpic_loadmap *)&defobj->loadmap, def->st_value);
			}
			where[1] = (Elf_Addr)defobj->pltgot;

			break;

		default:
			rdbg(("sym = %lu, type = %lu, offset = %p, "
			    "contents = %p",
			    (u_long)ELF_R_SYM(rel->r_info),
			    (u_long)ELF_R_TYPE(rel->r_info),
			    (void *)rel->r_offset, (void *)load_ptr(where)));
			_rtld_error("%s: Unsupported relocation type %ld "
			    "in non-PLT relocations",
			    obj->path, (u_long) ELF_R_TYPE(rel->r_info));
			return -1;
		}
	}
	return 0;
}

int
_rtld_relocate_plt_lazy(Obj_Entry *obj)
{
	const Elf_Rel *rel;

	for (rel = obj->pltrellim; rel-- > obj->pltrel; ) {
		Elf_Addr *where = (Elf_Addr *)rtld_relocate((struct elf_fdpic_loadmap *)&obj->loadmap, rel->r_offset);

		assert(ELF_R_TYPE(rel->r_info) == R_TYPE(JUMP_SLOT) ||
			ELF_R_TYPE(rel->r_info) == R_TYPE(IRELATIVE) ||
			ELF_R_TYPE(rel->r_info) == R_TYPE(FUNCDESC_VALUE));

		if (ELF_R_TYPE(rel->r_info) == R_TYPE(IRELATIVE))
			obj->ifunc_remaining = obj->pltrellim - rel;

		if (ELF_R_TYPE(rel->r_info) == R_TYPE(FUNCDESC_VALUE))
			where[1] = (Elf_Addr)obj->pltgot;

		/* Just relocate the GOT slots pointing into the PLT */
		*where = (Elf_Addr)rtld_relocate((struct elf_fdpic_loadmap *)&obj->loadmap, *where);
		rdbg(("fixup !main in %s --> %p", obj->path, (void *)*where));
	}

	return 0;
}

static int
_rtld_relocate_plt_object(const Obj_Entry *obj, const Elf_Rel *rel,
	struct elf_fdpic_desc **tp)
{
	struct elf_fdpic_desc *where = rtld_relocate((struct elf_fdpic_loadmap *)&obj->loadmap, rel->r_offset);
	Elf_Addr new_value;
	const Elf_Sym  *def;
	const Obj_Entry *defobj;
	unsigned long info = rel->r_info;

	assert(ELF_R_TYPE(info) == R_TYPE(FUNCDESC_VALUE));

	def = _rtld_find_plt_symdef(ELF_R_SYM(info), obj, &defobj, tp != NULL);
	if (__predict_false(def == NULL))
		return -1;
	if (__predict_false(def == &_rtld_sym_zero))
		return 0;

	if (ELF_ST_TYPE(def->st_info) == STT_GNU_IFUNC) {
		if (tp == NULL)
			return 0;
		new_value = _rtld_resolve_ifunc(defobj, def);
	} else {
		new_value = (Elf_Addr)rtld_relocate((struct elf_fdpic_loadmap *)&defobj->loadmap, def->st_value);
	}
	/* Set the Thumb bit, if needed.  */
	if (ELF_ST_TYPE(def->st_info) == STT_ARM_TFUNC)
		new_value |= 1;
	rdbg(("bind now/fixup in %s --> old=%p,%p new=%p,%p",
	    defobj->strtab + def->st_name, (void *)where->f, (void *)where->got,
		(void *)new_value, defobj->pltgot));
	where->f = new_value;
	where->got = (Elf32_Addr)defobj->pltgot;

	if (tp)
		*tp = where;

	return 0;
}

struct elf_fdpic_desc *
_rtld_bind(const void *objgot, Elf_Word reloff)
{
	const Obj_Entry *obj;
	const Elf_Rel *rel;
	struct elf_fdpic_desc *new_value = NULL;	/* XXX gcc */
	int err;

	for (obj = _rtld_objlist; obj != NULL; obj = obj->next) {
		if (obj->pltgot == objgot) {
			break;
		}
	}

	assert(obj != NULL);

	rel = (const Elf_Rel *)((const uint8_t *)obj->pltrel + reloff);

	_rtld_shared_enter();

	err = _rtld_relocate_plt_object(obj, rel, &new_value);

	if (err)
		_rtld_die();

	_rtld_shared_exit();

	return new_value;
}

int
_rtld_relocate_plt_objects(const Obj_Entry *obj)
{
	const Elf_Rel *rel;
	int err = 0;

	for (rel = obj->pltrel; rel < obj->pltrellim; rel++) {
		err = _rtld_relocate_plt_object(obj, rel, NULL);
		if (err)
			break;
	}

	return err;
}

Elf_Addr _rtld_function_descriptor_alloc(Obj_Entry *obj,
    const Elf_Sym *def, Elf_Addr addr)
{
	struct desc_lst {
		void *next;
		struct elf_fdpic_desc dsc;
	} *lst = NEW(struct desc_lst);

	if (lst == NULL)
		return -1;

	lst->dsc.got = (Elf_Addr)obj->pltgot;
	if (def == NULL) {
		lst->dsc.f = addr;
	} else {
		lst->dsc.f = (Elf_Addr)rtld_relocate((struct elf_fdpic_loadmap *)&obj->loadmap, def->st_value);
	}
	/* TODO: should we deduplicate entries? */
	lst->next = ((void **)obj->descs)[0];
	obj->descs = lst;

	return (Elf_Addr)&lst->dsc;
}

void _rtld_function_descriptor_free(void *desc)
{
	void *curr = desc;
	void *next;

	while (curr != NULL) {
		next = ((void **)curr)[0];
		xfree(curr);
		curr = next;
	}
}

const void *_rtld_function_descriptor_function(const void *dsc)
{
	/* Using FDPIC format there is no way to assert if passed value is a function descriptor or not.
	 * Thus we cannot provide this functionality. This probably breaks dladdr. */
	return dsc;
}

Elf_Addr _rtld_call_function_addr(const Obj_Entry *obj, Elf_Addr fn)
{
	volatile struct elf_fdpic_desc dsc = {
		.f   = fn,
		.got = (Elf_Addr)obj->pltgot,
	};
	Elf_Addr (*f)(void) = (Elf_Addr (*)(void))&dsc;

	return f();
}
