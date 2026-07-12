#include "aiqa_state_machine.h"

#include <stddef.h>

static aiqa_transition_t make_transition(
    aiqa_state_machine_t *machine,
    aiqa_state_t next_state,
    aiqa_error_code_t error,
    bool accepted)
{
    aiqa_transition_t transition = {
        .previous_state = machine->state,
        .next_state = next_state,
        .error = error,
        .changed = machine->state != next_state,
        .accepted = accepted,
    };

    if (accepted) {
        machine->state = next_state;
        machine->last_error = error;
        if (transition.changed) {
            machine->transition_count += 1;
        }
    }

    return transition;
}

static aiqa_transition_t transition_error(aiqa_state_machine_t *machine, aiqa_error_code_t error)
{
    return make_transition(machine, AIQA_STATE_ERROR, error, true);
}

static bool error_allows_ptt_retry(aiqa_error_code_t error)
{
    switch (error) {
    case AIQA_ERROR_AUDIO_TOO_LONG:
    case AIQA_ERROR_ASR_FAILED:
    case AIQA_ERROR_CHAT_FAILED:
    case AIQA_ERROR_TIMEOUT:
    case AIQA_ERROR_CANCELLED:
        return true;
    default:
        return false;
    }
}

void aiqa_state_machine_init(aiqa_state_machine_t *machine)
{
    if (machine == NULL) {
        return;
    }

    machine->state = AIQA_STATE_BOOT;
    machine->last_error = AIQA_ERROR_NONE;
    machine->transition_count = 0;
}

aiqa_transition_t aiqa_state_machine_dispatch(aiqa_state_machine_t *machine, aiqa_event_t event)
{
    if (machine == NULL) {
        aiqa_transition_t ignored = {
            .previous_state = AIQA_STATE_ERROR,
            .next_state = AIQA_STATE_ERROR,
            .error = AIQA_ERROR_CONFIG_CORRUPT,
            .changed = false,
            .accepted = false,
        };
        return ignored;
    }

    if (event.type == AIQA_EVENT_CANCEL) {
        switch (machine->state) {
        case AIQA_STATE_IDLE:
        case AIQA_STATE_IDLE_WITH_RESULT:
        case AIQA_STATE_RECORDING:
        case AIQA_STATE_TRANSCRIBING:
        case AIQA_STATE_ASR_JOB_PENDING:
        case AIQA_STATE_THINKING:
        case AIQA_STATE_ERROR:
            return make_transition(machine, AIQA_STATE_IDLE, AIQA_ERROR_CANCELLED, true);
        default:
            return make_transition(machine, machine->state, machine->last_error, false);
        }
    }

    if (event.type == AIQA_EVENT_FACTORY_RESET) {
        return make_transition(machine, AIQA_STATE_CONFIG_CHECK, AIQA_ERROR_NONE, true);
    }

    switch (event.type) {
    case AIQA_EVENT_CONFIG_MISSING:
        return transition_error(machine, AIQA_ERROR_CONFIG_MISSING);
    case AIQA_EVENT_CONFIG_CORRUPT:
        return transition_error(machine, AIQA_ERROR_CONFIG_CORRUPT);
    case AIQA_EVENT_NETWORK_FAILED:
        return transition_error(machine, AIQA_ERROR_NETWORK_FAILED);
    case AIQA_EVENT_AUTH_FAILED:
        return transition_error(machine, AIQA_ERROR_AUTH_FAILED);
    case AIQA_EVENT_TLS_FAILED:
        return transition_error(machine, AIQA_ERROR_TLS_FAILED);
    case AIQA_EVENT_CERT_TIME_INVALID:
        return transition_error(machine, AIQA_ERROR_CERT_TIME_INVALID);
    case AIQA_EVENT_RATE_LIMITED:
        return transition_error(machine, AIQA_ERROR_RATE_LIMITED);
    case AIQA_EVENT_PROVIDER_UNSUPPORTED:
        return transition_error(machine, AIQA_ERROR_PROVIDER_UNSUPPORTED);
    case AIQA_EVENT_AUDIO_TOO_LONG:
        return transition_error(machine, AIQA_ERROR_AUDIO_TOO_LONG);
    case AIQA_EVENT_ASR_FAILED:
        return transition_error(machine, AIQA_ERROR_ASR_FAILED);
    case AIQA_EVENT_CHAT_FAILED:
        return transition_error(machine, AIQA_ERROR_CHAT_FAILED);
    case AIQA_EVENT_TIMEOUT:
        return transition_error(machine, AIQA_ERROR_TIMEOUT);
    default:
        break;
    }

    switch (machine->state) {
    case AIQA_STATE_BOOT:
        if (event.type == AIQA_EVENT_BOOTED) {
            return make_transition(machine, AIQA_STATE_CONFIG_CHECK, AIQA_ERROR_NONE, true);
        }
        break;
    case AIQA_STATE_CONFIG_CHECK:
        if (event.type == AIQA_EVENT_CONFIG_READY) {
            return make_transition(machine, AIQA_STATE_NETWORK_CONNECTING, AIQA_ERROR_NONE, true);
        }
        break;
    case AIQA_STATE_NETWORK_CONNECTING:
        if (event.type == AIQA_EVENT_NETWORK_READY) {
            return make_transition(machine, AIQA_STATE_IDLE, AIQA_ERROR_NONE, true);
        }
        break;
    case AIQA_STATE_IDLE:
    case AIQA_STATE_IDLE_WITH_RESULT:
        if (event.type == AIQA_EVENT_CHAT_STARTED) {
            return make_transition(machine, AIQA_STATE_THINKING, AIQA_ERROR_NONE, true);
        }
        if (event.type == AIQA_EVENT_PRESS_START) {
            return make_transition(machine, AIQA_STATE_RECORDING, AIQA_ERROR_NONE, true);
        }
        break;
    case AIQA_STATE_RECORDING:
        if (event.type == AIQA_EVENT_PRESS_END) {
            return make_transition(machine, AIQA_STATE_TRANSCRIBING, AIQA_ERROR_NONE, true);
        }
        break;
    case AIQA_STATE_TRANSCRIBING:
        if (event.type == AIQA_EVENT_PRESS_START) {
            return make_transition(machine, AIQA_STATE_RECORDING, AIQA_ERROR_NONE, true);
        }
        if (event.type == AIQA_EVENT_ASR_STARTED) {
            return make_transition(machine, AIQA_STATE_TRANSCRIBING, AIQA_ERROR_NONE, true);
        }
        if (event.type == AIQA_EVENT_ASR_JOB_SUBMITTED) {
            return make_transition(machine, AIQA_STATE_ASR_JOB_PENDING, AIQA_ERROR_NONE, true);
        }
        if (event.type == AIQA_EVENT_ASR_DONE) {
            return make_transition(machine, AIQA_STATE_THINKING, AIQA_ERROR_NONE, true);
        }
        break;
    case AIQA_STATE_ASR_JOB_PENDING:
        if (event.type == AIQA_EVENT_PRESS_START) {
            return make_transition(machine, AIQA_STATE_RECORDING, AIQA_ERROR_NONE, true);
        }
        if (event.type == AIQA_EVENT_ASR_DONE) {
            return make_transition(machine, AIQA_STATE_THINKING, AIQA_ERROR_NONE, true);
        }
        break;
    case AIQA_STATE_THINKING:
        if (event.type == AIQA_EVENT_PRESS_START) {
            return make_transition(machine, AIQA_STATE_RECORDING, AIQA_ERROR_NONE, true);
        }
        if (event.type == AIQA_EVENT_CHAT_STARTED) {
            return make_transition(machine, AIQA_STATE_THINKING, AIQA_ERROR_NONE, true);
        }
        if (event.type == AIQA_EVENT_CHAT_TOKEN) {
            return make_transition(machine, AIQA_STATE_THINKING, AIQA_ERROR_NONE, true);
        }
        if (event.type == AIQA_EVENT_CHAT_DONE) {
            return make_transition(machine, AIQA_STATE_IDLE_WITH_RESULT, AIQA_ERROR_NONE, true);
        }
        break;
    case AIQA_STATE_ERROR:
        if (event.type == AIQA_EVENT_CONFIG_READY) {
            return make_transition(machine, AIQA_STATE_NETWORK_CONNECTING, AIQA_ERROR_NONE, true);
        }
        if (event.type == AIQA_EVENT_PRESS_START && error_allows_ptt_retry(machine->last_error)) {
            return make_transition(machine, AIQA_STATE_RECORDING, AIQA_ERROR_NONE, true);
        }
        break;
    default:
        break;
    }

    return make_transition(machine, machine->state, machine->last_error, false);
}

const char *aiqa_state_name(aiqa_state_t state)
{
    switch (state) {
    case AIQA_STATE_BOOT:
        return "BOOT";
    case AIQA_STATE_CONFIG_CHECK:
        return "CONFIG_CHECK";
    case AIQA_STATE_NETWORK_CONNECTING:
        return "NETWORK_CONNECTING";
    case AIQA_STATE_IDLE:
        return "IDLE";
    case AIQA_STATE_RECORDING:
        return "RECORDING";
    case AIQA_STATE_TRANSCRIBING:
        return "TRANSCRIBING";
    case AIQA_STATE_ASR_JOB_PENDING:
        return "ASR_JOB_PENDING";
    case AIQA_STATE_THINKING:
        return "THINKING";
    case AIQA_STATE_IDLE_WITH_RESULT:
        return "IDLE_WITH_RESULT";
    case AIQA_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

const char *aiqa_event_name(aiqa_event_type_t type)
{
    switch (type) {
    case AIQA_EVENT_BOOTED:
        return "BOOTED";
    case AIQA_EVENT_CONFIG_READY:
        return "CONFIG_READY";
    case AIQA_EVENT_CONFIG_MISSING:
        return "CONFIG_MISSING";
    case AIQA_EVENT_CONFIG_CORRUPT:
        return "CONFIG_CORRUPT";
    case AIQA_EVENT_NETWORK_CONNECTING:
        return "NETWORK_CONNECTING";
    case AIQA_EVENT_NETWORK_READY:
        return "NETWORK_READY";
    case AIQA_EVENT_NETWORK_FAILED:
        return "NETWORK_FAILED";
    case AIQA_EVENT_PRESS_START:
        return "PRESS_START";
    case AIQA_EVENT_PRESS_END:
        return "PRESS_END";
    case AIQA_EVENT_CANCEL:
        return "CANCEL";
    case AIQA_EVENT_AUDIO_TOO_LONG:
        return "AUDIO_TOO_LONG";
    case AIQA_EVENT_ASR_STARTED:
        return "ASR_STARTED";
    case AIQA_EVENT_ASR_JOB_SUBMITTED:
        return "ASR_JOB_SUBMITTED";
    case AIQA_EVENT_ASR_DONE:
        return "ASR_DONE";
    case AIQA_EVENT_ASR_FAILED:
        return "ASR_FAILED";
    case AIQA_EVENT_CHAT_STARTED:
        return "CHAT_STARTED";
    case AIQA_EVENT_CHAT_TOKEN:
        return "CHAT_TOKEN";
    case AIQA_EVENT_CHAT_DONE:
        return "CHAT_DONE";
    case AIQA_EVENT_CHAT_FAILED:
        return "CHAT_FAILED";
    case AIQA_EVENT_AUTH_FAILED:
        return "AUTH_FAILED";
    case AIQA_EVENT_TLS_FAILED:
        return "TLS_FAILED";
    case AIQA_EVENT_CERT_TIME_INVALID:
        return "CERT_TIME_INVALID";
    case AIQA_EVENT_RATE_LIMITED:
        return "RATE_LIMITED";
    case AIQA_EVENT_PROVIDER_UNSUPPORTED:
        return "PROVIDER_UNSUPPORTED";
    case AIQA_EVENT_TIMEOUT:
        return "TIMEOUT";
    case AIQA_EVENT_FACTORY_RESET:
        return "FACTORY_RESET";
    default:
        return "UNKNOWN";
    }
}

const char *aiqa_error_name(aiqa_error_code_t error)
{
    switch (error) {
    case AIQA_ERROR_NONE:
        return "NONE";
    case AIQA_ERROR_CONFIG_MISSING:
        return "CONFIG_MISSING";
    case AIQA_ERROR_CONFIG_CORRUPT:
        return "CONFIG_CORRUPT";
    case AIQA_ERROR_NETWORK_FAILED:
        return "NETWORK_FAILED";
    case AIQA_ERROR_AUTH_FAILED:
        return "AUTH_FAILED";
    case AIQA_ERROR_TLS_FAILED:
        return "TLS_FAILED";
    case AIQA_ERROR_CERT_TIME_INVALID:
        return "CERT_TIME_INVALID";
    case AIQA_ERROR_RATE_LIMITED:
        return "RATE_LIMITED";
    case AIQA_ERROR_PROVIDER_UNSUPPORTED:
        return "PROVIDER_UNSUPPORTED";
    case AIQA_ERROR_AUDIO_TOO_LONG:
        return "AUDIO_TOO_LONG";
    case AIQA_ERROR_ASR_FAILED:
        return "ASR_FAILED";
    case AIQA_ERROR_CHAT_FAILED:
        return "CHAT_FAILED";
    case AIQA_ERROR_TIMEOUT:
        return "TIMEOUT";
    case AIQA_ERROR_CANCELLED:
        return "CANCELLED";
    default:
        return "UNKNOWN";
    }
}
