#ifndef CHAO_DICT_H
#define CHAO_DICT_H

/**
    "hey, why not just use a string as a dictionary"
    why it's the best idea ever:
    - it works lol
    - no need to serialise it
    - it's hideously slow, forcing you to be careful with your data
    - makes people angry

    so it's just a string with \n-separated values, like
    pippo=1
    albowiem=zaprawde
*/

const char* char_dict_find_key(const char* dict, const char* key) {
    size_t n = strlen(key);
    const char* p = dict;

    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;

        if (strncmp(p, key, n) == 0) {
            const char* lead = p + n;

            while (*lead == ' ' || *lead == '\t')
                lead++;

            if (*lead == '=')
                return p;
        }

        p = strchr(p, '\n');
        if (!p)
            break;

        p++;
    }

    return NULL;
}

char* char_dict_create(int size) {
    char* dict = (char*)malloc(sizeof(char)*size);
    dict[0] = '\0';
    return dict;
}

void char_dict_erase(char* dict, const char* key) {
    char* start = (char*)char_dict_find_key(dict, key);
    if (!start) return;
    char* end = strchr(start, '\n');
    end = end ? end + 1 : start + strlen(start);
    memmove(start, end, strlen(end) + 1);
}

void char_dict_set(char* dict, const char* key, const char* value) {
    char_dict_erase(dict, key);
    sprintf(dict + strlen(dict), "\n%s=%s", key, value);
}

void char_dict_seti(char* dict, const char* key, int value) {
    char_dict_erase(dict, key);
    sprintf(dict + strlen(dict), "\n%s=%d", key, value);
}

void char_dict_setf(char* dict, const char* key, double value) {
    char_dict_erase(dict, key);
    sprintf(dict + strlen(dict), "\n%s=%.4f", key, value);
}

bool char_dict_has(char* dict, const char* key) {
    return char_dict_find_key(dict, key);
}

static const char* char_dict_value(const char* dict, const char* key) {
    const char* start = char_dict_find_key(dict, key);
    return start ? strchr(start, '=') + 1 : NULL;
}

void char_dict_get(char* dst, char* dict, const char* key, const char* default_value) {
    const char* value = char_dict_value(dict, key);
    if (!value) {
        strcpy(dst, default_value);
        return;
    }
    const char* end = strchr(value, '\n');
    size_t len = end ? (size_t)(end - value) : strlen(value);
    memcpy(dst, value, len);
    dst[len] = '\0';
}

int char_dict_geti(char* dict, const char* key, int default_value) {
    const char* value = char_dict_value(dict, key);
    return value && *value && *value != '\n' ? atoi(value) : default_value;
}

double char_dict_getf(char* dict, const char* key, double default_value) {
    const char* value = char_dict_value(dict, key);
    return value && *value && *value != '\n' ? atof(value) : default_value;
}

#endif // CHAO_DICT_H

