/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Mozilla browser.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications, Inc.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Radha Kulkarni <radha@netscape.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

// Local Includes
#include "nsSHEntry.h"
#include "nsXPIDLString.h"
#include "nsReadableUtils.h"
#include "nsIDocShellLoadInfo.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocument.h"
#include "nsIDOMDocument.h"
#include "nsISHistory.h"
#include "nsISHistoryInternal.h"
#include "nsDocShellEditorData.h"
#include "nsSHEntryShared.h"
#include "nsILayoutHistoryState.h"
#include "nsIContentViewer.h"
#include "nsISupportsArray.h"

namespace dom = mozilla::dom;

static PRUint32 gEntryID = 0;

//*****************************************************************************
//***    nsSHEntry: Object Management
//*****************************************************************************


nsSHEntry::nsSHEntry()
  : mLoadType(0)
  , mID(gEntryID++)
  , mScrollPositionX(0)
  , mScrollPositionY(0)
  , mURIWasModified(PR_FALSE)
{
  mShared = new nsSHEntryShared();
}

nsSHEntry::nsSHEntry(const nsSHEntry &other)
  : mShared(other.mShared)
  , mURI(other.mURI)
  , mReferrerURI(other.mReferrerURI)
  , mTitle(other.mTitle)
  , mPostData(other.mPostData)
  , mLoadType(0)         // XXX why not copy?
  , mID(other.mID)
  , mScrollPositionX(0)  // XXX why not copy?
  , mScrollPositionY(0)  // XXX why not copy?
  , mURIWasModified(other.mURIWasModified)
  , mStateData(other.mStateData)
{
}

static PRBool
ClearParentPtr(nsISHEntry* aEntry, void* /* aData */)
{
  if (aEntry) {
    aEntry->SetParent(nsnull);
  }
  return PR_TRUE;
}

nsSHEntry::~nsSHEntry()
{
  // Null out the mParent pointers on all our kids.
  mChildren.EnumerateForwards(ClearParentPtr, nsnull);
}

//*****************************************************************************
//    nsSHEntry: nsISupports
//*****************************************************************************

NS_IMPL_ISUPPORTS4(nsSHEntry, nsISHContainer, nsISHEntry, nsIHistoryEntry,
                   nsISHEntryInternal)

//*****************************************************************************
//    nsSHEntry: nsISHEntry
//*****************************************************************************

NS_IMETHODIMP nsSHEntry::SetScrollPosition(PRInt32 x, PRInt32 y)
{
  mScrollPositionX = x;
  mScrollPositionY = y;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetScrollPosition(PRInt32 *x, PRInt32 *y)
{
  *x = mScrollPositionX;
  *y = mScrollPositionY;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetURIWasModified(PRBool* aOut)
{
  *aOut = mURIWasModified;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetURIWasModified(PRBool aIn)
{
  mURIWasModified = aIn;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetURI(nsIURI** aURI)
{
  *aURI = mURI;
  NS_IF_ADDREF(*aURI);
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetURI(nsIURI* aURI)
{
  mURI = aURI;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetReferrerURI(nsIURI **aReferrerURI)
{
  *aReferrerURI = mReferrerURI;
  NS_IF_ADDREF(*aReferrerURI);
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetReferrerURI(nsIURI *aReferrerURI)
{
  mReferrerURI = aReferrerURI;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SetContentViewer(nsIContentViewer *aViewer)
{
  return mShared->SetContentViewer(aViewer);
}

NS_IMETHODIMP
nsSHEntry::GetContentViewer(nsIContentViewer **aResult)
{
  *aResult = mShared->mContentViewer;
  NS_IF_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::GetAnyContentViewer(nsISHEntry **aOwnerEntry,
                               nsIContentViewer **aResult)
{
  // Find a content viewer in the root node or any of its children,
  // assuming that there is only one content viewer total in any one
  // nsSHEntry tree
  GetContentViewer(aResult);
  if (*aResult) {
#ifdef DEBUG_PAGE_CACHE 
    printf("Found content viewer\n");
#endif
    *aOwnerEntry = this;
    NS_ADDREF(*aOwnerEntry);
    return NS_OK;
  }
  // The root SHEntry doesn't have a ContentViewer, so check child nodes
  for (PRInt32 i = 0; i < mChildren.Count(); i++) {
    nsISHEntry* child = mChildren[i];
    if (child) {
#ifdef DEBUG_PAGE_CACHE
      printf("Evaluating SHEntry child %d\n", i);
#endif
      child->GetAnyContentViewer(aOwnerEntry, aResult);
      if (*aResult) {
        return NS_OK;
      }
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SetSticky(PRBool aSticky)
{
  mShared->mSticky = aSticky;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::GetSticky(PRBool *aSticky)
{
  *aSticky = mShared->mSticky;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetTitle(PRUnichar** aTitle)
{
  // Check for empty title...
  if (mTitle.IsEmpty() && mURI) {
    // Default title is the URL.
    nsCAutoString spec;
    if (NS_SUCCEEDED(mURI->GetSpec(spec)))
      AppendUTF8toUTF16(spec, mTitle);
  }

  *aTitle = ToNewUnicode(mTitle);
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetTitle(const nsAString &aTitle)
{
  mTitle = aTitle;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetPostData(nsIInputStream** aResult)
{
  *aResult = mPostData;
  NS_IF_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetPostData(nsIInputStream* aPostData)
{
  mPostData = aPostData;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetLayoutHistoryState(nsILayoutHistoryState** aResult)
{
  *aResult = mShared->mLayoutHistoryState;
  NS_IF_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetLayoutHistoryState(nsILayoutHistoryState* aState)
{
  mShared->mLayoutHistoryState = aState;
  if (mShared->mLayoutHistoryState) {
    mShared->mLayoutHistoryState->
      SetScrollPositionOnly(!mShared->mSaveLayoutState);
  }

  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetLoadType(PRUint32 * aResult)
{
  *aResult = mLoadType;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetLoadType(PRUint32  aLoadType)
{
  mLoadType = aLoadType;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetID(PRUint32 * aResult)
{
  *aResult = mID;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetID(PRUint32  aID)
{
  mID = aID;
  return NS_OK;
}

nsSHEntryShared* nsSHEntry::GetSharedState()
{
  return mShared;
}

NS_IMETHODIMP nsSHEntry::GetIsSubFrame(PRBool * aFlag)
{
  *aFlag = mShared->mIsFrameNavigation;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetIsSubFrame(PRBool  aFlag)
{
  mShared->mIsFrameNavigation = aFlag;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetCacheKey(nsISupports** aResult)
{
  *aResult = mShared->mCacheKey;
  NS_IF_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetCacheKey(nsISupports* aCacheKey)
{
  mShared->mCacheKey = aCacheKey;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetSaveLayoutStateFlag(PRBool * aFlag)
{
  *aFlag = mShared->mSaveLayoutState;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetSaveLayoutStateFlag(PRBool  aFlag)
{
  mShared->mSaveLayoutState = aFlag;
  if (mShared->mLayoutHistoryState) {
    mShared->mLayoutHistoryState->SetScrollPositionOnly(!aFlag);
  }

  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetExpirationStatus(PRBool * aFlag)
{
  *aFlag = mShared->mExpired;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetExpirationStatus(PRBool  aFlag)
{
  mShared->mExpired = aFlag;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::GetContentType(nsACString& aContentType)
{
  aContentType = mShared->mContentType;
  return NS_OK;
}

NS_IMETHODIMP nsSHEntry::SetContentType(const nsACString& aContentType)
{
  mShared->mContentType = aContentType;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::Create(nsIURI * aURI, const nsAString &aTitle,
                  nsIInputStream * aInputStream,
                  nsILayoutHistoryState * aLayoutHistoryState,
                  nsISupports * aCacheKey, const nsACString& aContentType,
                  nsISupports* aOwner,
                  PRUint64 aDocShellID, PRBool aDynamicCreation)
{
  mURI = aURI;
  mTitle = aTitle;
  mPostData = aInputStream;

  // Set the LoadType by default to loadHistory during creation
  mLoadType = (PRUint32) nsIDocShellLoadInfo::loadHistory;

  mShared->mCacheKey = aCacheKey;
  mShared->mContentType = aContentType;
  mShared->mOwner = aOwner;
  mShared->mDocShellID = aDocShellID;
  mShared->mDynamicallyCreated = aDynamicCreation;

  // By default all entries are set false for subframe flag. 
  // nsDocShell::CloneAndReplace() which creates entries for
  // all subframe navigations, sets the flag to true.
  mShared->mIsFrameNavigation = PR_FALSE;

  // By default we save LayoutHistoryState
  mShared->mSaveLayoutState = PR_TRUE;
  mShared->mLayoutHistoryState = aLayoutHistoryState;

  //By default the page is not expired
  mShared->mExpired = PR_FALSE;

  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::Clone(nsISHEntry ** aResult)
{
  *aResult = new nsSHEntry(*this);
  NS_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::GetParent(nsISHEntry ** aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = mShared->mParent;
  NS_IF_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SetParent(nsISHEntry * aParent)
{
  /* parent not Addrefed on purpose to avoid cyclic reference
   * Null parent is OK
   *
   * XXX this method should not be scriptable if this is the case!!
   */
  mShared->mParent = aParent;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SetWindowState(nsISupports *aState)
{
  mShared->mWindowState = aState;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::GetWindowState(nsISupports **aState)
{
  NS_IF_ADDREF(*aState = mShared->mWindowState);
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SetViewerBounds(const nsIntRect &aBounds)
{
  mShared->mViewerBounds = aBounds;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::GetViewerBounds(nsIntRect &aBounds)
{
  aBounds = mShared->mViewerBounds;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::GetOwner(nsISupports **aOwner)
{
  NS_IF_ADDREF(*aOwner = mShared->mOwner);
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SetOwner(nsISupports *aOwner)
{
  mShared->mOwner = aOwner;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::GetBFCacheEntry(nsIBFCacheEntry **aEntry)
{
  NS_ENSURE_ARG_POINTER(aEntry);
  NS_IF_ADDREF(*aEntry = mShared);
  return NS_OK;
}

PRBool
nsSHEntry::HasBFCacheEntry(nsIBFCacheEntry *aEntry)
{
  return static_cast<nsIBFCacheEntry*>(mShared) == aEntry;
}

NS_IMETHODIMP
nsSHEntry::AdoptBFCacheEntry(nsISHEntry *aEntry)
{
  nsCOMPtr<nsISHEntryInternal> shEntry = do_QueryInterface(aEntry);
  NS_ENSURE_STATE(shEntry);

  nsSHEntryShared *shared = shEntry->GetSharedState();
  NS_ENSURE_STATE(shared);

  mShared = shared;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SharesDocumentWith(nsISHEntry *aEntry, PRBool *aOut)
{
  NS_ENSURE_ARG_POINTER(aOut);

  nsCOMPtr<nsISHEntryInternal> internal = do_QueryInterface(aEntry); 
  NS_ENSURE_STATE(internal);

  *aOut = mShared == internal->GetSharedState();
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::AbandonBFCacheEntry()
{
  mShared = nsSHEntryShared::Duplicate(mShared);
  return NS_OK;
}

//*****************************************************************************
//    nsSHEntry: nsISHContainer
//*****************************************************************************

NS_IMETHODIMP 
nsSHEntry::GetChildCount(PRInt32 * aCount)
{
  *aCount = mChildren.Count();
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::AddChild(nsISHEntry * aChild, PRInt32 aOffset)
{
  if (aChild) {
    NS_ENSURE_SUCCESS(aChild->SetParent(this), NS_ERROR_FAILURE);
  }

  if (aOffset < 0) {
    mChildren.AppendObject(aChild);
    return NS_OK;
  }

  //
  // Bug 52670: Ensure children are added in order.
  //
  //  Later frames in the child list may load faster and get appended
  //  before earlier frames, causing session history to be scrambled.
  //  By growing the list here, they are added to the right position.
  //
  //  Assert that aOffset will not be so high as to grow us a lot.
  //
  NS_ASSERTION(aOffset < (mChildren.Count()+1023), "Large frames array!\n");

  PRBool newChildIsDyn = PR_FALSE;
  if (aChild) {
    aChild->IsDynamicallyAdded(&newChildIsDyn);
  }

  // If the new child is dynamically added, try to add it to aOffset, but if
  // there are non-dynamically added children, the child must be after those.
  if (newChildIsDyn) {
    PRInt32 lastNonDyn = aOffset - 1;
    for (PRInt32 i = aOffset; i < mChildren.Count(); ++i) {
      nsISHEntry* entry = mChildren[i];
      if (entry) {
        PRBool dyn = PR_FALSE;
        entry->IsDynamicallyAdded(&dyn);
        if (dyn) {
          break;
        } else {
          lastNonDyn = i;
        }
      }
    }
    // InsertObjectAt allows only appending one object.
    // If aOffset is larger than Count(), we must first manually
    // set the capacity.
    if (aOffset > mChildren.Count()) {
      mChildren.SetCount(aOffset);
    }
    if (!mChildren.InsertObjectAt(aChild, lastNonDyn + 1)) {
      NS_WARNING("Adding a child failed!");
      aChild->SetParent(nsnull);
      return NS_ERROR_FAILURE;
    }
  } else {
    // If the new child isn't dynamically added, it should be set to aOffset.
    // If there are dynamically added children before that, those must be
    // moved to be after aOffset.
    if (mChildren.Count() > 0) {
      PRInt32 start = NS_MIN(mChildren.Count() - 1, aOffset);
      PRInt32 dynEntryIndex = -1;
      nsISHEntry* dynEntry = nsnull;
      for (PRInt32 i = start; i >= 0; --i) {
        nsISHEntry* entry = mChildren[i];
        if (entry) {
          PRBool dyn = PR_FALSE;
          entry->IsDynamicallyAdded(&dyn);
          if (dyn) {
            dynEntryIndex = i;
            dynEntry = entry;
          } else {
            break;
          }
        }
      }
  
      if (dynEntry) {
        nsCOMArray<nsISHEntry> tmp;
        tmp.SetCount(aOffset - dynEntryIndex + 1);
        mChildren.InsertObjectsAt(tmp, dynEntryIndex);
        NS_ASSERTION(mChildren[aOffset + 1] == dynEntry, "Whaat?");
      }
    }
    

    // Make sure there isn't anything at aOffset.
    if (aOffset < mChildren.Count()) {
      nsISHEntry* oldChild = mChildren[aOffset];
      if (oldChild && oldChild != aChild) {
        NS_ERROR("Adding a child where we already have a child? This may misbehave");
        oldChild->SetParent(nsnull);
      }
    }

    if (!mChildren.ReplaceObjectAt(aChild, aOffset)) {
      NS_WARNING("Adding a child failed!");
      aChild->SetParent(nsnull);
      return NS_ERROR_FAILURE;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::RemoveChild(nsISHEntry * aChild)
{
  NS_ENSURE_TRUE(aChild, NS_ERROR_FAILURE);
  PRBool childRemoved = PR_FALSE;
  PRBool dynamic = PR_FALSE;
  aChild->IsDynamicallyAdded(&dynamic);
  if (dynamic) {
    childRemoved = mChildren.RemoveObject(aChild);
  } else {
    PRInt32 index = mChildren.IndexOfObject(aChild);
    if (index >= 0) {
      childRemoved = mChildren.ReplaceObjectAt(nsnull, index);
    }
  }
  if (childRemoved)
    aChild->SetParent(nsnull);
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::GetChildAt(PRInt32 aIndex, nsISHEntry ** aResult)
{
  if (aIndex >= 0 && aIndex < mChildren.Count()) {
    *aResult = mChildren[aIndex];
    // yes, mChildren can have holes in it.  AddChild's offset parameter makes
    // that possible.
    NS_IF_ADDREF(*aResult);
  } else {
    *aResult = nsnull;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::AddChildShell(nsIDocShellTreeItem *aShell)
{
  NS_ASSERTION(aShell, "Null child shell added to history entry");
  mShared->mChildShells.AppendObject(aShell);
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::ChildShellAt(PRInt32 aIndex, nsIDocShellTreeItem **aShell)
{
  NS_IF_ADDREF(*aShell = mShared->mChildShells.SafeObjectAt(aIndex));
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::ClearChildShells()
{
  mShared->mChildShells.Clear();
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::GetRefreshURIList(nsISupportsArray **aList)
{
  NS_IF_ADDREF(*aList = mShared->mRefreshURIList);
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SetRefreshURIList(nsISupportsArray *aList)
{
  mShared->mRefreshURIList = aList;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SyncPresentationState()
{
  return mShared->SyncPresentationState();
}

void
nsSHEntry::RemoveFromBFCacheSync()
{
  mShared->RemoveFromBFCacheSync();
}

void
nsSHEntry::RemoveFromBFCacheAsync()
{
  mShared->RemoveFromBFCacheAsync();
}

nsDocShellEditorData*
nsSHEntry::ForgetEditorData()
{
  // XXX jlebar Check how this is used.
  return mShared->mEditorData.forget();
}

void
nsSHEntry::SetEditorData(nsDocShellEditorData* aData)
{
  NS_ASSERTION(!(aData && mShared->mEditorData),
               "We're going to overwrite an owning ref!");
  if (mShared->mEditorData != aData) {
    mShared->mEditorData = aData;
  }
}

PRBool
nsSHEntry::HasDetachedEditor()
{
  return mShared->mEditorData != nsnull;
}

NS_IMETHODIMP
nsSHEntry::GetStateData(nsIStructuredCloneContainer **aContainer)
{
  NS_ENSURE_ARG_POINTER(aContainer);
  NS_IF_ADDREF(*aContainer = mStateData);
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SetStateData(nsIStructuredCloneContainer *aContainer)
{
  mStateData = aContainer;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::IsDynamicallyAdded(PRBool* aAdded)
{
  *aAdded = mShared->mDynamicallyCreated;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::HasDynamicallyAddedChild(PRBool* aAdded)
{
  *aAdded = PR_FALSE;
  for (PRInt32 i = 0; i < mChildren.Count(); ++i) {
    nsISHEntry* entry = mChildren[i];
    if (entry) {
      entry->IsDynamicallyAdded(aAdded);
      if (*aAdded) {
        break;
      }
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::GetDocshellID(PRUint64* aID)
{
  *aID = mShared->mDocShellID;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SetDocshellID(PRUint64 aID)
{
  mShared->mDocShellID = aID;
  return NS_OK;
}


NS_IMETHODIMP
nsSHEntry::GetLastTouched(PRUint32 *aLastTouched)
{
  *aLastTouched = mShared->mLastTouched;
  return NS_OK;
}

NS_IMETHODIMP
nsSHEntry::SetLastTouched(PRUint32 aLastTouched)
{
  mShared->mLastTouched = aLastTouched;
  return NS_OK;
}
