/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_MIRROR_FIELD_INL_H_
#define ART_RUNTIME_MIRROR_FIELD_INL_H_

#include "field.h"

#include "art_field-inl.h"
#include "class-alloc-inl.h"
#include "class_root-inl.h"
#include "dex_cache-inl.h"
#include "object-inl.h"

namespace art {

namespace mirror {

inline ObjPtr<mirror::Class> Field::GetDeclaringClass() REQUIRES_SHARED(Locks::mutator_lock_) {
  return GetFieldObject<Class>(OFFSET_OF_OBJECT_MEMBER(Field, declaring_class_));
}

inline Primitive::Type Field::GetTypeAsPrimitiveType() {
  return GetType()->GetPrimitiveType();
}

inline ObjPtr<mirror::Class> Field::GetType() {
  return GetFieldObject<mirror::Class>(OFFSET_OF_OBJECT_MEMBER(Field, type_));
}

template <PointerSize kPointerSize>
inline ObjPtr<mirror::Field> Field::CreateFromArtField(Thread* self,
                                                       ArtField* field,
                                                       bool force_resolve) {
  StackHandleScope<2> hs(self);
  // Try to resolve type before allocating since this is a thread suspension point.
  Handle<mirror::Class> type = hs.NewHandle(field->ResolveType());

  if (type == nullptr) {
    if (force_resolve) {
      if (kIsDebugBuild) {
        self->AssertPendingException();
      }
      return nullptr;
    } else {
      // Can't resolve, clear the exception if it isn't OOME and continue with a null type.
      mirror::Throwable* exception = self->GetException();
      if (exception->GetClass()->DescriptorEquals("Ljava/lang/OutOfMemoryError;")) {
        return nullptr;
      }
      self->ClearException();
    }
  }
  auto ret = hs.NewHandle(ObjPtr<Field>::DownCast(GetClassRoot<Field>()->AllocObject(self)));
  if (UNLIKELY(ret == nullptr)) {
    self->AssertPendingOOMException();
    return nullptr;
  }
  auto dex_field_index = field->GetDexFieldIndex();
  auto* resolved_field = field->GetDexCache()->GetResolvedField(dex_field_index, kPointerSize);
  if (field->GetDeclaringClass()->IsProxyClass()) {
    DCHECK(field->IsStatic());
    DCHECK_LT(dex_field_index, 2U);
    // The two static fields (interfaces, throws) of all proxy classes
    // share the same dex file indices 0 and 1. So, we can't resolve
    // them in the dex cache.
  } else {
    if (resolved_field != nullptr) {
      DCHECK_EQ(resolved_field, field);
    } else {
      // We rely on the field being resolved so that we can back to the ArtField
      // (i.e. FromReflectedMethod).
      field->GetDexCache()->SetResolvedField(dex_field_index, field, kPointerSize);
    }
  }
  // We're initializing a newly allocated object, so we do not need to record that under
  // a transaction. If the transaction is aborted, the whole object shall be unreachable.
  ret->SetType</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(type.Get());
  ret->SetDeclaringClass</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      field->GetDeclaringClass());
  ret->SetAccessFlags</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      field->GetAccessFlags());
  auto iter_range = field->IsStatic() ? field->GetDeclaringClass()->GetSFields()
                                      : field->GetDeclaringClass()->GetIFields();
  auto position = std::find_if(
      iter_range.begin(), iter_range.end(), [&](const auto& f) { return &f == field; });
  DCHECK(position != iter_range.end());
  ret->SetArtFieldIndex</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      std::distance(iter_range.begin(), position));
  ret->SetOffset</*kTransactionActive=*/ false, /*kCheckTransaction=*/ false>(
      field->GetOffset().Int32Value());
  return ret.Get();
}

template<bool kTransactionActive, bool kCheckTransaction>
inline void Field::SetDeclaringClass(ObjPtr<Class> c) {
  SetFieldObject<kTransactionActive, kCheckTransaction>(DeclaringClassOffset(), c);
}

template<bool kTransactionActive, bool kCheckTransaction>
inline void Field::SetType(ObjPtr<Class> type) {
  SetFieldObject<kTransactionActive, kCheckTransaction>(TypeOffset(), type);
}

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_FIELD_INL_H_
