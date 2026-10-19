"""Post-parse alias-resolution pass for the UnDOS syscall/struct IDL.

Resolves every `using`-declared alias reference (`AliasType`) into the
concrete `FieldType` it refers to. Scalar aliases retain their public typedef
name as a CType. Must run after parse_idl() and before any generate_* pass.
"""

from .ast_nodes import (
    AliasType,
    ArrayType,
    BufferType,
    CType,
    DynArrayType,
    FieldType,
    IDLModule,
)


class ResolverError(Exception):
    pass


def resolve_aliases(module: IDLModule) -> None:
    alias_map = {u.name: u for u in module.usings}

    def resolve(ftype: FieldType, chain=(), preserve_scalar_names=False) -> FieldType:
        if isinstance(ftype, AliasType):
            if ftype.name in chain:
                raise ResolverError(f"Circular 'using' alias: {' -> '.join(chain + (ftype.name,))}")
            if ftype.name not in alias_map:
                raise ResolverError(f"Unknown type or alias '{ftype.name}'")
            resolved = resolve(alias_map[ftype.name].aliased, chain + (ftype.name,),
                               preserve_scalar_names)
            if preserve_scalar_names and isinstance(resolved, CType):
                return CType(ftype.name)
            return resolved
        if isinstance(ftype, (ArrayType, DynArrayType, BufferType)):
            element = resolve(ftype.elem_type, chain, preserve_scalar_names)
            return ArrayType(element, ftype.size) if isinstance(ftype, ArrayType) else type(ftype)(element)
        return ftype

    for sc in list(module.syscalls) + list(module.structs):
        for field in sc.fields:
            field.ftype = resolve(field.ftype, preserve_scalar_names=True)

    for fl in module.flags:
        if fl.underlying in alias_map:
            resolved = resolve(AliasType(fl.underlying))
            if not isinstance(resolved, CType):
                raise ResolverError(
                    f"'flags {fl.name}' underlying alias '{fl.underlying}' must resolve to a c<T> type"
                )
            fl.underlying = resolved.ctype

    for u in module.usings:
        u.aliased = resolve(u.aliased, (u.name,))
