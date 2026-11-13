#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Parse only the variant selectors; CMake still owns all other build options.
rv1126b_boolean() {
    case $(printf '%s' "$1" | tr '[:lower:]' '[:upper:]') in
        1|ON|YES|TRUE|Y) printf '%s' ON ;;
        0|OFF|NO|FALSE|N|'') printf '%s' OFF ;;
        *) echo "Expected a CMake boolean, got: $1" >&2; return 2 ;;
    esac
}

rv1126b_parse_options() {
    profile_language=zh-CN
    profile_video=OFF
    profile_ubsan=OFF
    profile_pending_definition=0
    for profile_argument do
        if [ "$profile_pending_definition" = 1 ]; then
            profile_definition=$profile_argument
            profile_pending_definition=0
        else
            case "$profile_argument" in
                -D) profile_pending_definition=1; continue ;;
                -D*) profile_definition=${profile_argument#-D} ;;
                *) continue ;;
            esac
        fi
        case "$profile_definition" in
            *=*) ;;
            *) continue ;;
        esac
        profile_name=${profile_definition%%=*}
        profile_name=${profile_name%%:*}
        profile_value=${profile_definition#*=}
        case "$profile_name" in
            RV1126B_LANGUAGE) profile_language=$profile_value ;;
            MYBOT_ENABLE_VIDEO) profile_video=$(rv1126b_boolean "$profile_value") || return $? ;;
            RV1126B_ENABLE_UBSAN) profile_ubsan=$(rv1126b_boolean "$profile_value") || return $? ;;
        esac
    done
    case "$profile_language" in
        zh-CN) profile_locale_suffix=zh ;;
        en-US) profile_locale_suffix=en ;;
        *) echo "RV1126B_LANGUAGE must be zh-CN or en-US" >&2; return 2 ;;
    esac
}
