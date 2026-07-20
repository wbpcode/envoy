#pragma once

#include "envoy/common/optref.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Common {

/**
 * A self-managing, move-able pair of non-owning back-references used to make async callbacks
 * safe against either side being destroyed first, WITHOUT requiring the callbacks object to be
 * owned by a shared_ptr (so no std::weak_ptr is available) and WITHOUT a heap allocation.
 *
 * The pair is:
 *   - CallbacksHandler<C> : typically owned/embedded by the entity that issues the async request
 *     and is interested in the result. Its lifetime gates whether the callbacks are still "live".
 *   - CallbacksHandler<C>::CallbacksWrapper : the token handed to the async machinery (stored in a
 *     std::function, a member, a container, ...). The async code calls wrapper.callbacks() and,
 *     only if it still has_value(), invokes the callbacks.
 *
 * Each side holds an OptRef to the other. When either side is cancel()led or destroyed it nulls out
 * the peer's back-reference, so neither can ever dangle:
 *   - Destroying the handler clears the wrapper's callbacks_, so wrapper.callbacks() becomes empty
 *     and the async side learns the requester is gone.
 *   - Destroying the wrapper clears the handler's pointer to it, so the handler's own destruction
 *     is a no-op.
 *
 * The cycle is broken by onPeerCancel(): the side that initiates teardown clears its own pointer
 * FIRST and then calls the peer's onPeerCancel(), which only nulls fields and never calls back.
 * Hence no infinite recursion and no use-after-clear.
 *
 * Establishing a pair (the handler must out-exist the wrapper's construction call):
 *   CallbacksHandler<MyCallbacks> handler;
 *   CallbacksHandler<MyCallbacks>::CallbacksWrapper wrapper(handler, my_callbacks); // wires both ways
 *   // hand `wrapper` (by move) to the async operation; keep `handler` near `my_callbacks`.
 *
 * Threading: NOT thread-safe. Both ends must live on, and be mutated from, the same thread
 * (e.g. the same dispatcher). If the async completion can fire from another thread, this is the
 * wrong tool -- use ThreadSafeCallbackManager instead.
 *
 * Copy/move: these are move-only types. Copy is explicitly deleted -- copying would alias the
 * single back-reference the peer holds and immediately dangle. Move is defined to re-point the
 * peer at the new address, which is what lets callers keep the pair on the stack / inline instead
 * of paying for a heap allocation.
 *
 * Extending the classes: CallbacksWrapper::cancel()/onPeerCancel() (and its destructor) are virtual
 * so a subclass can observe teardown. IMPORTANT for extenders -- to stay move-able (the whole point
 * of this type), a subclass MUST NOT hand-declare a destructor or any copy/move operation, because
 * doing so suppresses the implicitly-generated move operations and, since copy is deleted, leaves
 * the subclass immovable. Put cleanup in an override of cancel()/onPeerCancel(), not in a
 * destructor. Note the base destructor calls CallbacksWrapper::cancel() non-virtually, so a subclass
 * override of cancel() does NOT run during base destruction -- override onPeerCancel() (called when
 * the peer tears the link down) or do per-instance cleanup in member destruction order instead.
 * (CallbacksHandler's own cancel()/onPeerCancel() are non-virtual and not extension points.)
 */
template <class CallbacksType> class CallbacksHandler {
public:
  class CallbacksWrapper {
  public:
    CallbacksWrapper(const CallbacksWrapper&) = delete;
    CallbacksWrapper& operator=(const CallbacksWrapper&) = delete;

    CallbacksWrapper(CallbacksHandler<CallbacksType>& handler, CallbacksType& callbacks)
        : handler_(handler), callbacks_(callbacks) {
      ASSERT(!handler.wrapper_.has_value(), "CallbacksHandler already has a wrapper");
      handler.wrapper_ = OptRef<CallbacksWrapper>(*this);
    }

    CallbacksWrapper(CallbacksWrapper&& to_move) noexcept { moveFrom(to_move); }

    CallbacksWrapper& operator=(CallbacksWrapper&& to_move) noexcept {
      if (this != &to_move) {
        // Detach our current handler (clears its back-pointer) and our callbacks before adopting.
        cancel();
        moveFrom(to_move);
      }
      return *this;
    }

    virtual ~CallbacksWrapper() { CallbacksWrapper::cancel(); }

    /**
     * @return an OptRef to the callbacks, empty if the handler has been destroyed or cancelled. The
     *         async side should call this and only invoke the callbacks if has_value() is true.
     */
    OptRef<CallbacksType> callbacks() const { return callbacks_; }

    /**
     * Actively tears down this side of the link: forgets the callbacks and tells the handler to
     * drop its pointer to this wrapper. Idempotent.
     */
    virtual void cancel() {
      callbacks_.reset();
      auto handler = handler_;
      handler_.reset();

      if (handler.has_value()) {
        handler->onPeerCancel();
      }
    }

  protected:
    // Called BY the handler when the handler is the one tearing the link down. Only nulls fields;
    // never calls back into the handler (that is what breaks the teardown cycle).
    virtual void onPeerCancel() {
      handler_.reset();
      callbacks_.reset();
    }

    // The handler needs access to onPeerCancel()/handler_ on wrapper instances other than the one
    // it encloses. (The reverse direction needs no friend: a nested class already has access to its
    // enclosing class's members.)
    friend class CallbacksHandler;

    OptRef<CallbacksHandler<CallbacksType>> handler_;
    OptRef<CallbacksType> callbacks_;

  private:
    // Re-points `to_move`'s handler back at this wrapper, adopts its link, and empties the source.
    // Shared by the move constructor and move assignment; assumes any link previously held on this
    // side is already gone (the constructor starts empty, the assignment calls reset() first).
    void moveFrom(CallbacksWrapper& to_move) noexcept {
      if (to_move.handler_.has_value()) {
        to_move.handler_->wrapper_ = OptRef<CallbacksWrapper>(*this);
      }
      handler_ = to_move.handler_;
      callbacks_ = to_move.callbacks_;
      to_move.handler_.reset();
      to_move.callbacks_.reset();
    }
  };

  CallbacksHandler(const CallbacksHandler&) = delete;
  CallbacksHandler& operator=(const CallbacksHandler&) = delete;

  CallbacksHandler() = default;

  CallbacksHandler(CallbacksHandler&& to_move) noexcept { moveFrom(to_move); }

  CallbacksHandler& operator=(CallbacksHandler&& to_move) noexcept {
    if (this != &to_move) {
      // Virtual: runs any subclass teardown, then drops our current wrapper's back-pointer.
      cancel();
      moveFrom(to_move);
    }
    return *this;
  }

  // Destructor tears down the link via cancel(). CallbacksHandler::cancel() is non-virtual, so
  // (unlike the wrapper) there is no override to run here anyway.
  ~CallbacksHandler() { CallbacksHandler::cancel(); }

  /**
   * Actively tears down this side of the link: tells the wrapper to forget its callbacks (so the
   * async side sees callbacks() as empty) and drops our pointer to it. Idempotent.
   */
  void cancel() {
    auto wrapper = wrapper_;
    wrapper_.reset();

    if (wrapper.has_value()) {
      wrapper->onPeerCancel();
    }
  }

protected:
  // Called BY the wrapper when the wrapper is the one tearing the link down. Only nulls our
  // pointer; never calls back into the wrapper.
  void onPeerCancel() { wrapper_.reset(); }

  OptRef<CallbacksWrapper> wrapper_;

private:
  // Re-points `to_move`'s wrapper back at this handler, adopts its link, and empties the source.
  // Shared by the move constructor and move assignment; assumes any link previously held on this
  // side is already gone (the constructor starts empty, the assignment calls reset() first).
  void moveFrom(CallbacksHandler& to_move) noexcept {
    if (to_move.wrapper_.has_value()) {
      to_move.wrapper_->handler_ = OptRef<CallbacksHandler>(*this);
    }
    wrapper_ = to_move.wrapper_;
    to_move.wrapper_.reset();
  }
};

} // namespace Common
} // namespace Envoy
