/* vim: set expandtab ts=4 sw=4: */
/*
 * You may redistribute this program and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "memory/Allocator.h"
#include "util/events/libuv/EventBase_pvt.h"
#include "util/events/Event.h"
#include "util/Identity.h"

#include <stddef.h>
#include <stdint.h>
#include <uv.h>

struct Event_pvt
{
    struct Event pub;
    void (* const callback)(void* callbackContext);
    void* const callbackContext;
    uv_poll_t handler;
    struct Allocator* alloc;
    Identity
};

static void handleEvent(uv_poll_t* handle, int status, int events)
{
    struct Event_pvt* event =
        Identity_cast((struct Event_pvt*) (((char*)handle) - offsetof(struct Event_pvt, handler)));

    if ((status == 0) && (events & UV_READABLE)) {
        event->callback(event->callbackContext);
    }
}

static void freeEvent2(uv_handle_t* handle)
{
    struct Allocator_OnFreeJob* job = Identity_cast((struct Allocator_OnFreeJob*)handle->data);
    job->complete(job);
}

static int freeEvent(struct Allocator_OnFreeJob* job)
{
    struct Event_pvt* event = Identity_cast((struct Event_pvt*) job->userData);
    event->handler.data = job;
    uv_close((uv_handle_t*) &event->handler, freeEvent2);
    return Allocator_ONFREE_ASYNC;
}

struct Event* Event_socketRead(void (* const callback)(void* callbackContext),
                               void* const callbackContext,
                               int s,
                               struct EventBase* eventBase,
                               struct Allocator* allocator,
                               struct Except* eh)
{
    struct EventBase_pvt* base = Identity_cast((struct EventBase_pvt*) eventBase);
    struct Allocator* alloc = Allocator_child(allocator);
    struct Event_pvt* out = Allocator_clone(alloc, (&(struct Event_pvt) {
        .callback = callback,
        .callbackContext = callbackContext,
        .alloc = alloc
    }));
    Identity_set(out);

    if (uv_poll_init(base->loop, &out->handler, s) != 0) {
        Allocator_free(alloc);
        Except_raise(eh, Event_socketRead_INTERNAL, "Failed to create event. errno [%s]",
                     uv_strerror(uv_last_error(base->loop)));
    }

    if (uv_poll_start(&out->handler, UV_READABLE, handleEvent) == -1) {
        Allocator_free(alloc);
        Except_raise(eh, Event_socketRead_INTERNAL, "Failed to register event. errno [%s]",
                     uv_strerror(uv_last_error(base->loop)));
    }

    out->handler.data = out;

    Allocator_onFree(alloc, freeEvent, out);

    return &out->pub;
}
