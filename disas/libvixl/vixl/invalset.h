// Copyright 2015, ARM Limited
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of ARM Limited nor the names of its contributors may be
//     used to endorse or promote products derived from this software without
//     specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef VIXL_INVALSET_H_
#define VIXL_INVALSET_H_

#include <string.h>

#include <algorithm>
#include <vector>

#include "vixl/globals.h"

namespace vixl {

// We define a custom data structure template and its iterator as `std`
// containers do not fit the performance requirements for some of our use cases.
//
// The structure behaves like an iterable unordered set with special properties
// and restrictions. "InvalSet" stands for "Invalidatable Set".
//
// Restrictions and requirements:
// - Adding an element already present in the set is illegal. In debug mode,
//   this is checked at insertion time.
// - The templated class `ElementType` must provide comparison operators so that
//   `std::sort()` can be used.
// - A key must be available to represent invalid elements.
// - Elements with an invalid key must compare higher or equal to any other
//   element.
//
// Use cases and performance considerations:
// Our use cases present two specificities that allow us to design this
// structure to provide fast insertion *and* fast search and deletion
// operations:
// - Elements are (generally) inserted in order (sorted according to their key).
// - A key is available to mark elements as invalid (deleted).
// The backing `std::vector` allows for fast insertions. When
// searching for an element we ensure the elements are sorted (this is generally
// the case) and perform a binary search. When deleting an element we do not
// free the associated memory immediately. Instead, an element to be deleted is
// marked with the 'invalid' key. Other methods of the container take care of
// ignoring entries marked as invalid.
// To avoid the overhead of the `std::vector` container when only few entries
// are used, a number of elements are preallocated.

// 'ElementType' and 'KeyType' are respectively the types of the elements and
// their key.  The structure only reclaims memory when safe to do so, if the
// number of elements that can be reclaimed is greater than `RECLAIM_FROM` and
// greater than `<total number of elements> / RECLAIM_FACTOR.
#define TEMPLATE_INVALSET_P_DECL                                               \
  class ElementType,                                                           \
  unsigned N_PREALLOCATED_ELEMENTS,                                            \
  class KeyType,                                                               \
  KeyType INVALID_KEY,                                                         \
  size_t RECLAIM_FROM,                                                         \
  unsigned RECLAIM_FACTOR

#define TEMPLATE_INVALSET_P_DEF                                                \
ElementType, N_PREALLOCATED_ELEMENTS,                                          \
KeyType, INVALID_KEY, RECLAIM_FROM, RECLAIM_FACTOR

template<class S> class InvalSetIterator;  // Forward declaration.

template<TEMPLATE_INVALSET_P_DECL> class InvalSet {
 public:
  InvalSet();
  ~InvalSet();

  static const size_t kNPreallocatedElements = N_PREALLOCATED_ELEMENTS;
  static const KeyType kInvalidKey = INVALID_KEY;

  // It is illegal to insert an element already present in the set.
  void insert(const ElementType& element);

  // Looks for the specified element in the set and - if found - deletes it.
  void erase(const ElementType& element);

  // This indicates the number of (valid) elements stored in this set.
  size_t size() const;

  // Returns true if no elements are stored in the set.
  // Note that this does not mean the the backing storage is empty: it can still
  // contain invalid elements.
  bool empty() const;

  void clear();

  const ElementType min_element();

  // This returns the key of the minimum element in the set.
  KeyType min_element_key();

  static bool IsValid(const ElementType& element);
  static KeyType Key(const ElementType& element);
  static void SetKey(ElementType* element, KeyType key);

 protected:
  // Returns a pointer to the element in vector_ if it was found, or NULL
  // otherwise.
  ElementType* Search(const ElementType& element);

  // The argument *must* point to an element stored in *this* set.
  // This function is not allowed to move elements in the backing vector
  // storage.
  void EraseInternal(ElementType* element);

  // The elements in the range searched must be sorted.
  ElementType* BinarySearch(const ElementType& element,
                            ElementType* start,
                            ElementType* end) const;

  // Sort the elements.
  enum SortType {
    // The 'hard' version guarantees that invalid elements are moved to the end
    // of the container.
    kHardSort,
    // The 'soft' version only guarantees that the elements will be sorted.
    // Invalid elements may still be present anywhere in the set.
    kSoftSort
  };
  void Sort(SortType sort_type);

  // Delete the elements that have an invalid key. The complexity is linear
  // with the size of the vector.
  void Clean();

  const ElementType Front() const;
  const ElementType Back() const;

  // Delete invalid trailing elements and return the last valid element in the
  // set.
  const ElementType CleanBack();

  // Returns a pointer to the start or end of the backing storage.
  const ElementType* StorageBegin() const;
  const ElementType* StorageEnd() const;
  ElementType* StorageBegin();
  ElementType* StorageEnd();

  // Returns the index of the element within the backing storage. The element
  // must belong to the backing storage.
  size_t ElementIndex(const ElementType* element) const;

  // Returns the element at the specified index in the backing storage.
  const ElementType* ElementAt(size_t index) const;
  ElementType* ElementAt(size_t index);

  static const ElementType* FirstValidElement(const ElementType* from,
                                              const ElementType* end);

  void CacheMinElement();
  const ElementType CachedMinElement() const;

  bool ShouldReclaimMemory() const;
  void ReclaimMemory();

  bool IsUsingVector() const { return vector_ != NULL; }
  void set_sorted(bool sorted) { sorted_ = sorted; }

  // We cache some data commonly required by users to improve performance.
  // We cannot cache pointers to elements as we do not control the backing
  // storage.
  bool valid_cached_min_;
  size_t cached_min_index_;  // Valid iff `valid_cached_min_` is true.
  KeyType cached_min_key_;         // Valid iff `valid_cached_min_` is true.

  // Indicates whether the elements are sorted.
  bool sorted_;

  // This represents the number of (valid) elements in this set.
  size_t size_;

  // The backing storage is either the array of preallocated elements or the
  // vector. The structure starts by using the preallocated elements, and
  // transitions (permanently) to using the vector once more than
  // kNPreallocatedElements are used.
  // Elements are only invalidated when using the vector. The preallocated
  // storage always only contains valid elements.
  ElementType preallocated_[kNPreallocatedElements];
  std::vector<ElementType>* vector_;

#ifdef VIXL_DEBUG
  // Iterators acquire and release this monitor. While a set is acquired,
  // certain operations are illegal to ensure that the iterator will
  // correctly iterate over the elements in the set.
  int monitor_;
  int monitor() const { return monitor_; }
  void Acquire() { monitor_++; }
  void Release() {
    monitor_--;
    VIXL_ASSERT(monitor_ >= 0);
  }
#endif

  friend class InvalSetIterator<InvalSet<TEMPLATE_INVALSET_P_DEF> >;
  typedef ElementType _ElementType;
  typedef KeyType _KeyType;
};


template<class S> class InvalSetIterator {
 private:
  // Redefine types to mirror the associated set types.
  typedef typename S::_ElementType ElementType;
  typedef typename S::_KeyType KeyType;

 public:
  explicit InvalSetIterator(S* inval_set);
  ~InvalSetIterator();

  ElementType* Current() const;
  void Advance();
  bool Done() const;

  // Mark this iterator as 'done'.
  void Finish();

  // Delete the current element and advance the iterator to point to the next
  // element.
  void DeleteCurrentAndAdvance();

  static bool IsValid(const ElementType& element);
  static KeyType Key(const ElementType& element);

 protected:
  void MoveToValidElement();

  // Indicates if the iterator is looking at the vector or at the preallocated
  // elements.
  const bool using_vector_;
  // Used when looking at the preallocated elements, or in debug mode when using
  // the vector to track how many times the iterator has advanced.
  size_t index_;
  typename std::vector<ElementType>::iterator iterator_;
  S* inval_set_;
};


template<TEMPLATE_INVALSET_P_DECL>
InvalSet<TEMPLATE_INVALSET_P_DEF>::InvalSet()
  : valid_cached_min_(false),
    sorted_(true), size_(0), vector_(NULL) {
#ifdef VIXL_DEBUG
  monitor_ = 0;
#endif
}


template<TEMPLATE_INVALSET_P_DECL>
InvalSet<TEMPLATE_INVALSET_P_DEF>::~InvalSet() {
  VIXL_ASSERT(monitor_ == 0);
  delete vector_;
}


template<TEMPLATE_INVALSET_P_DECL>
void InvalSet<TEMPLATE_INVALSET_P_DEF>::insert(const ElementType& element) {
  VIXL_ASSERT(monitor() == 0);
  VIXL_ASSERT(IsValid(element));
  VIXL_ASSERT(Search(element) == NULL);
  set_sorted(empty() || (sorted_ && (element > CleanBack())));
  if (IsUsingVector()) {
    vector_->push_back(element);
  } else {
    if (size_ < kNPreallocatedElements) {
      preallocated_[size_] = element;
    } else {
      // Transition to using the vector.
      vector_ = new std::vector<ElementType>(preallocated_,
                                             preallocated_ + size_);
      vector_->push_back(element);
    }
  }
  size_++;

  if (valid_cached_min_ && (element < min_element())) {
    cached_min_index_ = IsUsingVector() ? vector_->size() - 1 : size_ - 1;
    cached_min_key_ = Key(element);
    valid_cached_min_ = true;
  }

  if (ShouldReclaimMemory()) {
    ReclaimMemory();
  }
}


template<TEMPLATE_INVALSET_P_DECL>
void InvalSet<TEMPLATE_INVALSET_P_DEF>::erase(const ElementType& element) {
  VIXL_ASSERT(monitor() == 0);
  VIXL_ASSERT(IsValid(element));
  ElementType* local_element = Search(element);
  if (local_element != NULL) {
    EraseInternal(local_element);
  }
}


template<TEMPLATE_INVALSET_P_DECL>
ElementType* InvalSet<TEMPLATE_INVALSET_P_DEF>::Search(
    const ElementType& element) {
  VIXL_ASSERT(monitor() == 0);
  if (empty()) {
    return NULL;
  }
  if (ShouldReclaimMemory()) {
    ReclaimMemory();
  }
  if (!sorted_) {
    Sort(kHardSort);
  }
  if (!valid_cached_min_) {
    CacheMinElement();
  }
  return BinarySearch(element, ElementAt(cached_min_index_), StorageEnd());
}


template<TEMPLATE_INVALSET_P_DECL>
size_t InvalSet<TEMPLATE_INVALSET_P_DEF>::size() const {
  return size_;
}


template<TEMPLATE_INVALSET_P_DECL>
bool InvalSet<TEMPLATE_INVALSET_P_DEF>::empty() const {
  return size_ == 0;
}


template<TEMPLATE_INVALSET_P_DECL>
void InvalSet<TEMPLATE_INVALSET_P_DEF>::clear() {
  VIXL_ASSERT(monitor() == 0);
  size_ = 0;
  if (IsUsingVector()) {
    vector_->clear();
  }
  set_sorted(true);
  valid_cached_min_ = false;
}


template<TEMPLATE_INVALSET_P_DECL>
const ElementType InvalSet<TEMPLATE_INVALSET_P_DEF>::min_element() {
  VIXL_ASSERT(monitor() == 0);
  VIXL_ASSERT(!empty());
  CacheMinElement();
  return *ElementAt(cached_min_index_);
}


template<TEMPLATE_INVALSET_P_DECL>
KeyType InvalSet<TEMPLATE_INVALSET_P_DEF>::min_element_key() {
  VIXL_ASSERT(monitor() == 0);
  if (valid_cached_min_) {
    return cached_min_key_;
  } else {
    return Key(min_element());
  }
}


template<TEMPLATE_INVALSET_P_DECL>
bool InvalSet<TEMPLATE_INVALSET_P_DEF>::IsValid(const ElementType& element) {
  return Key(element) != kInvalidKey;
}


template<TEMPLATE_INVALSET_P_DECL>
void InvalSet<TEMPLATE_INVALSET_P_DEF>::EraseInternal(ElementType* element) {
  // Note that this function must be safe even while an iterator has acquired
  // this set.
  VIXL_ASSERT(element != NULL);
  size_t deleted_index = ElementIndex(element);
  if (IsUsingVector()) {
    VIXL_ASSERT((&(vector_->front()) <= element) &&
                (element <= &(vector_->back())));
    SetKey(element, kInvalidKey);
  } else {
    VIXL_ASSERT((preallocated_ <= element) &&
                (element < (preallocated_ + kNPreallocatedElements)));
    ElementType* end = preallocated_ + kNPreallocatedElements;
    size_t copy_size = sizeof(*element) * (end - element - 1);
    memmove(element, element + 1, copy_size);
  }
  size_--;

  if (valid_cached_min_ &&
      (deleted_index == cached_min_index_)) {
    if (sorted_ && !empty()) {
      const ElementType* min = FirstValidElement(element, StorageEnd());
      cached_min_index_ = ElementIndex(min);
      cached_min_key_ = Key(*min);
      valid_cached_min_ = true;
    } else {
      valid_cached_min_ = false;
    }
  }
}


template<TEMPLATE_INVALSET_P_DECL>
ElementType* InvalSet<TEMPLATE_INVALSET_P_DEF>::BinarySearch(
    const ElementType& element, ElementType* start, ElementType* end) const {
  if (start == end) {
    return NULL;
  }
  VIXL_ASSERT(sorted_);
  VIXL_ASSERT(start < end);
  VIXL_ASSERT(!empty());

  // Perform a binary search through the elements while ignoring invalid
  // elements.
  ElementType* elements = start;
  size_t low = 0;
  size_t high = (end - start) - 1;
  while (low < high) {
    // Find valid bounds.
    while (!IsValid(elements[low]) && (low < high)) ++low;
    while (!IsValid(elements[high]) && (low < high)) --high;
    VIXL_ASSERT(low <= high);
    // Avoid overflow when computing the middle index.
    size_t middle = low / 2 + high / 2 + (low & high & 1);
    if ((middle == low) || (middle == high)) {
      break;
    }
    while (!IsValid(elements[middle]) && (middle < high - 1)) ++middle;
    while (!IsValid(elements[middle]) && (low + 1 < middle)) --middle;
    if (!IsValid(elements[middle])) {
      break;
    }
    if (elements[middle] < element) {
      low = middle;
    } else {
      high = middle;
    }
  }

  if (elements[low] == element) return &elements[low];
  if (elements[high] == element) return &elements[high];
  return NULL;
}


template<TEMPLATE_INVALSET_P_DECL>
void InvalSet<TEMPLATE_INVALSET_P_DEF>::Sort(SortType sort_type) {
  VIXL_ASSERT(monitor() == 0);
  if (sort_type == kSoftSort) {
    if (sorted_) {
      return;
    }
  }
  if (empty()) {
    return;
  }

  Clean();
  std::sort(StorageBegin(), StorageEnd());

  set_sorted(true);
  cached_min_index_ = 0;
  cached_min_key_ = Key(Front());
  valid_cached_min_ = true;
}


template<TEMPLATE_INVALSET_P_DECL>
void InvalSet<TEMPLATE_INVALSET_P_DEF>::Clean() {
  VIXL_ASSERT(monitor() == 0);
  if (empty() || !IsUsingVector()) {
    return;
  }
  // Manually iterate through the vector storage to discard invalid elements.
  ElementType* start = &(vector_->front());
  ElementType* end = start + vector_->size();
  ElementType* c = start;
  ElementType* first_invalid;
  ElementType* first_valid;
  ElementType* next_invalid;

  while (c < end && IsValid(*c)) { c++; }
  first_invalid = c;

  while (c < end) {
    while (c < end && !IsValid(*c)) { c++; }
    first_valid = c;
    while (c < end && IsValid(*c)) { c++; }
    next_invalid = c;

    ptrdiff_t n_moved_elements = (next_invalid - first_valid);
    memmove(first_invalid, first_valid,  n_moved_elements * sizeof(*c));
    first_invalid = first_invalid + n_moved_elements;
    c = next_invalid;
  }

  // Delete the trailing invalid elements.
  vector_->erase(vector_->begin() + (first_invalid - start), vector_->end());
  VIXL_ASSERT(vector_->size() == size_);

  if (sorted_) {
    valid_cached_min_ = true;
    cached_min_index_ = 0;
    cached_min_key_ = Key(*ElementAt(0));
  } else {
    valid_cached_min_ = false;
  }
}


template<TEMPLATE_INVALSET_P_DECL>
const ElementType InvalSet<TEMPLATE_INVALSET_P_DEF>::Front() const {
  VIXL_ASSERT(!empty());
  return IsUsingVector() ? vector_->front() : preallocated_[0];
}


template<TEMPLATE_INVALSET_P_DECL>
const ElementType InvalSet<TEMPLATE_INVALSET_P_DEF>::Back() const {
  VIXL_ASSERT(!empty());
  return IsUsingVector() ? vector_->back() : preallocated_[size_ - 1];
}


template<TEMPLATE_INVALSET_P_DECL>
const ElementType InvalSet<TEMPLATE_INVALSET_P_DEF>::CleanBack() {
  VIXL_ASSERT(monitor() == 0);
  if (IsUsingVector()) {
    // Delete the invalid trailing elements.
    typename std::vector<ElementType>::reverse_iterator it = vector_->rbegin();
    while (!IsValid(*it)) {
      it++;
    }
    vector_->erase(it.base(), vector_->end());
  }
  return Back();
}


template<TEMPLATE_INVALSET_P_DECL>
const ElementType* InvalSet<TEMPLATE_INVALSET_P_DEF>::StorageBegin() const {
  return IsUsingVector() ? &(vector_->front()) : preallocated_;
}


template<TEMPLATE_INVALSET_P_DECL>
const ElementType* InvalSet<TEMPLATE_INVALSET_P_DEF>::StorageEnd() const {
  return IsUsingVector() ? &(vector_->back()) + 1 : preallocated_ + size_;
}


template<TEMPLATE_INVALSET_P_DECL>
ElementType* InvalSet<TEMPLATE_INVALSET_P_DEF>::StorageBegin() {
  return IsUsingVector() ? &(vector_->front()) : preallocated_;
}


template<TEMPLATE_INVALSET_P_DECL>
ElementType* InvalSet<TEMPLATE_INVALSET_P_DEF>::StorageEnd() {
  return IsUsingVector() ? &(vector_->back()) + 1 : preallocated_ + size_;
}


template<TEMPLATE_INVALSET_P_DECL>
size_t InvalSet<TEMPLATE_INVALSET_P_DEF>::ElementIndex(
    const ElementType* element) const {
  VIXL_ASSERT((StorageBegin() <= element) && (element < StorageEnd()));
  return element - StorageBegin();
}


template<TEMPLATE_INVALSET_P_DECL>
const ElementType* InvalSet<TEMPLATE_INVALSET_P_DEF>::ElementAt(
    size_t index) const {
  VIXL_ASSERT(
      (IsUsingVector() && (index < vector_->size())) || (index < size_));
  return StorageBegin() + index;
}

template<TEMPLATE_INVALSET_P_DECL>
ElementType* InvalSet<TEMPLATE_INVALSET_P_DEF>::ElementAt(size_t index) {
  VIXL_ASSERT(
      (IsUsingVector() && (index < vector_->size())) || (index < size_));
  return StorageBegin() + index;
}

template<TEMPLATE_INVALSET_P_DECL>
const ElementType* InvalSet<TEMPLATE_INVALSET_P_DEF>::FirstValidElement(
    const ElementType* from, const ElementType* end) {
  while ((from < end) && !IsValid(*from)) {
    from++;
  }
  return from;
}


template<TEMPLATE_INVALSET_P_DECL>
void InvalSet<TEMPLATE_INVALSET_P_DEF>::CacheMinElement() {
  VIXL_ASSERT(monitor() == 0);
  VIXL_ASSERT(!empty());

  if (valid_cached_min_) {
    return;
  }

  if (sorted_) {
    const ElementType* min = FirstValidElement(StorageBegin(), StorageEnd());
    cached_min_index_ = ElementIndex(min);
    cached_min_key_ = Key(*min);
    valid_cached_min_ = true;
  } else {
    Sort(kHardSort);
  }
  VIXL_ASSERT(valid_cached_min_);
}


template<TEMPLATE_INVALSET_P_DECL>
bool InvalSet<TEMPLATE_INVALSET_P_DEF>::ShouldReclaimMemory() const {
  if (!IsUsingVector()) {
    return false;
  }
  size_t n_invalid_elements = vector_->size() - size_;
  return (n_invalid_elements > RECLAIM_FROM) &&
         (n_invalid_elements > vector_->size() / RECLAIM_FACTOR);
}


template<TEMPLATE_INVALSET_P_DECL>
void InvalSet<TEMPLATE_INVALSET_P_DEF>::ReclaimMemory() {
  VIXL_ASSERT(monitor() == 0);
  Clean();
}


template<class S>
InvalSetIterator<S>::InvalSetIterator(S* inval_set)
    : using_vector_((inval_set != NULL) && inval_set->IsUsingVector()),
      index_(0),
      inval_set_(inval_set) {
  if (inval_set != NULL) {
    inval_set->Sort(S::kSoftSort);
#ifdef VIXL_DEBUG
    inval_set->Acquire();
#endif
    if (using_vector_) {
      iterator_ = typename std::vector<ElementType>::iterator(
          inval_set_->vector_->begin());
    }
    MoveToValidElement();
  }
}


template<class S>
InvalSetIterator<S>::~InvalSetIterator() {
#ifdef VIXL_DEBUG
  if (inval_set_ != NULL) {
    inval_set_->Release();
  }
#endif
}


template<class S>
typename S::_ElementType* InvalSetIterator<S>::Current() const {
  VIXL_ASSERT(!Done());
  if (using_vector_) {
    return &(*iterator_);
  } else {
    return &(inval_set_->preallocated_[index_]);
  }
}


template<class S>
void InvalSetIterator<S>::Advance() {
  VIXL_ASSERT(!Done());
  if (using_vector_) {
    iterator_++;
#ifdef VIXL_DEBUG
    index_++;
#endif
    MoveToValidElement();
  } else {
    index_++;
  }
}


template<class S>
bool InvalSetIterator<S>::Done() const {
  if (using_vector_) {
    bool done = (iterator_ == inval_set_->vector_->end());
    VIXL_ASSERT(done == (index_ == inval_set_->size()));
    return done;
  } else {
    return index_ == inval_set_->size();
  }
}


template<class S>
void InvalSetIterator<S>::Finish() {
  VIXL_ASSERT(inval_set_->sorted_);
  if (using_vector_) {
    iterator_ = inval_set_->vector_->end();
  }
  index_ = inval_set_->size();
}


template<class S>
void InvalSetIterator<S>::DeleteCurrentAndAdvance() {
  if (using_vector_) {
    inval_set_->EraseInternal(&(*iterator_));
    MoveToValidElement();
  } else {
    inval_set_->EraseInternal(inval_set_->preallocated_ + index_);
  }
}


template<class S>
bool InvalSetIterator<S>::IsValid(const ElementType& element) {
  return S::IsValid(element);
}


template<class S>
typename S::_KeyType InvalSetIterator<S>::Key(const ElementType& element) {
  return S::Key(element);
}


template<class S>
void InvalSetIterator<S>::MoveToValidElement() {
  if (using_vector_) {
    while ((iterator_ != inval_set_->vector_->end()) && !IsValid(*iterator_)) {
      iterator_++;
    }
  } else {
    VIXL_ASSERT(inval_set_->empty() || IsValid(inval_set_->preallocated_[0]));
    // Nothing to do.
  }
}

#undef TEMPLATE_INVALSET_P_DECL
#undef TEMPLATE_INVALSET_P_DEF

}  // namespace vixl

#endif  // VIXL_INVALSET_H_
