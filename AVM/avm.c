#include "avm.h"
#include "reader.h"
#include <stdarg.h>
#include <string.h>
#include <math.h>

char *typeStrings[] = {
    "number",
    "string",
    "bool",
    "table",
    "userfunc",
    "libfunc",
    "nil",
    "undef"
};

void avm_error(char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stdout,"\033[0;31mAVM:ERROR: ");
    vfprintf(stdout, format, args);
    fprintf(stdout,"\033[0m\n");
    va_end(args);
    executionFinished = 1;
}

void avm_warning(char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stdout,"\033[0;33mAVM:WARNING: ");
    vfprintf(stdout, format, args);
    fprintf(stdout,"\033[0m\n");
    va_end(args);
    warnings++;
}
// ---------------------------------------------------------------------------
// DYNAMIC ARRAYS
// ---------------------------------------------------------------------------

avm_memcell *avm_translate_operand (struct vmarg *arg, struct avm_memcell *reg) {
    switch (arg->type) {
        // VARIABLES
        // enviroment function!
        case global_a:  return &stack[AVM_STACKSIZE-1-arg->val];
        case local_a:   return &stack[topsp-arg->val];
        case formal_a:  return &stack[topsp+AVM_STACKENV_SIZE+1+arg->val];
        
        case retval_a:  return &retval;
        // CONSTANTS
        case number_a:
            reg->type = number_m;
            reg->data.numVal = consts_getnumber(arg->val);
            return reg;
        case string_a:
            reg->type = string_m;
            reg->data.strVal = consts_getstring(arg->val);
            return reg;
        case bool_a:
            reg->type = bool_m;
            reg->data.boolVal = arg->val;
            return reg;
        case nil_a:
            reg->type = nil_m;
            return reg;
        //  FUNCTIONS
        case userfunc_a:
            reg->type = userfunc_m;
            reg->data.funcVal = userFuncs[arg->val].address;
            return reg;
        case libfunc_a:
            reg->type = libfunc_m;
            reg->data.libfuncVal = libfuncs_getused(arg->val);
            return reg;
        default: 
            assert(0);
    }
}


// ---------------------------------------------------------------------------
// DISPATCHER
// ---------------------------------------------------------------------------
extern void execute_assign (struct instruction*);
extern void execute_add (struct instruction*);
extern void execute_sub (struct instruction*);
extern void execute_mul (struct instruction*);
extern void execute_div (struct instruction*);
extern void execute_mod (struct instruction*);
extern void execute_uminus (struct instruction*);
extern void execute_and (struct instruction*);
extern void execute_or (struct instruction*);
extern void execute_not (struct instruction*);
extern void execute_jeq (struct instruction*);
extern void execute_jne (struct instruction*);
extern void execute_jle (struct instruction*);
extern void execute_jge (struct instruction*);
extern void execute_jlt (struct instruction*);
extern void execute_jgt (struct instruction*);
extern void execute_jump (struct instruction*);
extern void execute_call (struct instruction*);
extern void execute_pusharg (struct instruction*);
extern void execute_funcenter (struct instruction*);
extern void execute_funcexit (struct instruction*);
extern void execute_newtable (struct instruction*);
extern void execute_tablegetelem (struct instruction*);
extern void execute_tablesetelem (struct instruction*);
extern void execute_nop (struct instruction*);

typedef void (*execute_func_t)(struct instruction *);

execute_func_t executeFuncs[] = {
    execute_assign,
    execute_add,
    execute_sub,
    execute_mul,
    execute_div,
    execute_mod,
    execute_uminus,
    execute_and,
    execute_or,
    execute_not,
    execute_jeq,
    execute_jne,
    execute_jle,
    execute_jge,
    execute_jlt,
    execute_jgt,
    execute_jump, // added extra
    execute_call,
    execute_pusharg,
    execute_funcenter,
    execute_funcexit,
    execute_newtable,
    execute_tablegetelem,
    execute_tablesetelem,
    execute_nop
};

void execution_cycle (void) {
    if (executionFinished) return;
    if (pc == AVM_ENDING_PC) {
        executionFinished = 1;
        return;
    }
    assert(pc < AVM_ENDING_PC);
    struct instruction *instr = code + pc;
    assert(instr->opcode >=0 && instr->opcode <= AVM_MAX_INSTRUCTIONS);
    if (instr->srcLine) currLine = instr->srcLine; // DEAL WITH SCRLINE IN READER
    unsigned oldPC = pc;

    // printf("\033[0;33mExec PC:%u  TOP:%u OP:%u\033[0m\n",pc,top,instr->opcode); 
    (*executeFuncs[instr->opcode])(instr);
    if (pc == oldPC) ++pc;
    // print_stack();
    
}

// ---------------------------------------------------------------------------
// INSTRUCTION IMPLEMENTATION
// ---------------------------------------------------------------------------
extern void memclear_string (struct avm_memcell *m) {
    assert(m->data.strVal);
    free(m->data.strVal);
}

extern void memclear_table (struct avm_memcell *m) {
    assert(m->data.tableVal);
    avm_tabledecrefcounter(m->data.tableVal);
}
memclear_func_t memclearFuncs[] = {
    0, // NUMBER
    memclear_string,
    0, // BOOLEAN
    memclear_table,
    0, // USERFUNC
    0, // LIBFUNC
    0, // NIL
    0  // UNDEF
};

void avm_memcellclear(struct avm_memcell *m) {
    if (m->type != undef_m) {
        memclear_func_t f = memclearFuncs[m->type];
        if (f) (*f)(m);
        m->type = undef_m;
    }
}

// extern void avm_callsaveenvironment(void);
void avm_callsaveenvironment (void) {
    avm_push_envvalue(totalActuals);
    avm_push_envvalue(pc+1);
    avm_push_envvalue(top + totalActuals + 2);
    avm_push_envvalue(topsp);
}


void avm_dec_top(void) {
    if (!top) {
        // STACK OVERFLOW
        avm_error("Stack Overflow!");
        executionFinished = 1;
        return;
    }
    top--;
}

void avm_push_envvalue(unsigned val) {
    stack[top].type = number_m;
    stack[top].data.numVal = val;
    avm_dec_top();
}

struct userfunc *avm_getfuncinfo(unsigned address) {
    return &userFuncs[code[address].result.val];
}

unsigned avm_get_envvalue(unsigned i) {
    assert(stack[i].type == number_m);
    unsigned val = (unsigned) stack[i].data.numVal;
    assert(stack[i].data.numVal == ((double) val));
    return val;
}

library_func_t avm_getlibraryfunc(char *id){
    unsigned i;
    for (i=0; i<totalNamedLibFuncs; i++) if (!strcmp(id, namedLibFuncs[i])) break;
    if (i == totalNamedLibFuncs) avm_error("Libfunc '%s' not found!\n", id);
    return library_func_t_addresses[i];
}

// extern void avm_calllibfunc(char *funcName);
void avm_calllibfunc(char *id) {
    library_func_t f = avm_getlibraryfunc(id);
    if (!f) avm_error("Unsupported lib func '%s' called!", id);
    topsp = top;
    totalActuals = 0;
    (*f)();
    if (!executionFinished) execute_funcexit((struct instruction *) 0);
}


unsigned avm_totalactuals(void) {
    return avm_get_envvalue(topsp + AVM_NUMACTUALS_OFFSET);
}

struct avm_memcell *avm_getactual(unsigned i) {
    assert(i < avm_totalactuals());
    return &stack[topsp + AVM_STACKENV_SIZE + 1 + i];
}

void avm_registerlibfunc(char *id, library_func_t addr){
    unsigned i;
    for (i=0; i<totalNamedLibFuncs; i++) if (!strcmp(id, namedLibFuncs[i])) break;
    if (i == totalNamedLibFuncs) return;
    library_func_t_addresses[i] = addr;
}


// ------------------- STRINGS

char *number_tostring(struct avm_memcell *x){
    assert(x->type == number_m);
    char *s = (char *) malloc(sizeof(char) * 100);
    sprintf(s, "%f", x->data.numVal);
    return s;
}
char *string_tostring(struct avm_memcell *x){
    assert(x->type == string_m);
    return strdup(x->data.strVal);
}
char *bool_tostring(struct avm_memcell *x){
    assert(x->type == bool_m);
    if (x->data.boolVal) return strdup("true");
    return strdup("false");
}
char *table_tostring(struct avm_memcell *x){
    assert(x->type == table_m);
    struct avm_table_bucket *bucket;
    char *buff = (char *) malloc(128), *key, *value;
    unsigned i;
    size_t curr_buff_size = 128, pair_size, curr_buff_filled_size = 0;
    for (i = 0; i<AVM_TABLE_HASHSIZE; i++) {
        bucket = x->data.tableVal->numIndexed[i];
        while (bucket) {
            key = avm_tostring(&bucket->key);
            value = avm_tostring(&bucket->value);
            pair_size = strlen(key) + strlen(value) + 5;
            while (curr_buff_filled_size + pair_size + 2 > curr_buff_size) {
                buff = realloc(buff, 2*curr_buff_size); 
                curr_buff_size *= 2;
            }
            sprintf(buff + curr_buff_filled_size, "{%s:%s}, ", key, value);
            curr_buff_filled_size += pair_size;
            free(key);
            free(value);
            bucket = bucket->next;
        }
        bucket = x->data.tableVal->strIndexed[i];
        while (bucket) {
            key = avm_tostring(&bucket->key);
            value = avm_tostring(&bucket->value);
            pair_size = strlen(key) + strlen(value) + 7;
            while (curr_buff_filled_size + pair_size + 2 > curr_buff_size) {
                buff = realloc(buff, 2*curr_buff_size);
                curr_buff_size *= 2;
            }
            sprintf(buff + curr_buff_filled_size, "{'%s':%s}, ", key, value);
            curr_buff_filled_size += pair_size;
            free(key);
            free(value);
            bucket = bucket->next;
        }
        bucket = x->data.tableVal->boolIndexed[i];
        while (bucket) {
            key = avm_tostring(&bucket->key);
            value = avm_tostring(&bucket->value);
            pair_size = strlen(key) + strlen(value) + 5;
            while (curr_buff_filled_size + pair_size + 2 > curr_buff_size) {
                buff = realloc(buff, 2*curr_buff_size);
                curr_buff_size *= 2;
            }
            sprintf(buff + curr_buff_filled_size, "{%s:%s}, ", key, value);
            curr_buff_filled_size += pair_size;
            free(key);
            free(value);
            bucket = bucket->next;
        }
        bucket = x->data.tableVal->ufncIndexed[i];
        while (bucket) {
            key = avm_tostring(&bucket->key);
            value = avm_tostring(&bucket->value);
            pair_size = strlen(key) + strlen(value) + 5;
            while (curr_buff_filled_size + pair_size + 2 > curr_buff_size) {
                buff = realloc(buff, 2*curr_buff_size);
                curr_buff_size *= 2;
            }
            sprintf(buff + curr_buff_filled_size, "{%s:%s}, ", key, value);
            curr_buff_filled_size += pair_size;
            free(key);
            free(value);
            bucket = bucket->next;
        }
        bucket = x->data.tableVal->lfncIndexed[i];
        while (bucket) {
            key = avm_tostring(&bucket->key);
            value = avm_tostring(&bucket->value);
            pair_size = strlen(key) + strlen(value) + 5;
            while (curr_buff_filled_size + pair_size + 2 > curr_buff_size) {
                buff = realloc(buff, 2*curr_buff_size);
                curr_buff_size *= 2;
            }
            sprintf(buff + curr_buff_filled_size, "{%s:%s}, ", key, value);
            curr_buff_filled_size += pair_size;
            free(key);
            free(value);
            bucket = bucket->next;
        }
    }
    sprintf(buff + curr_buff_filled_size-2, "\0\0");
    return buff;
}
char *userfunc_tostring(struct avm_memcell *x){
    assert(x->type == userfunc_m);
    struct userfunc* f = avm_getfuncinfo(x->data.funcVal);
    unsigned address = f->address;
    unsigned n = strlen(f->id) + 50; // 29 = 26 for static + 13 for uint + \0
    char *s = (char *) malloc(sizeof(char) * n);
    sprintf(s, "userfunction: %s , address: %u", f->id, f->address);
    return s;
}
char *libfunc_tostring(struct avm_memcell *x){
    assert(x->type == libfunc_m);
    return strdup(x->data.libfuncVal);
}
char *nil_tostring(struct avm_memcell *x){
    assert(x->type == nil_m);
    return strdup("nil");
}
char *undef_tostring(struct avm_memcell *x){
    assert(x->type == undef_m);
    return strdup("undef");
}

tostring_func_t tostringFuncs[] = {
    number_tostring,
    string_tostring,
    bool_tostring,
    table_tostring,
    userfunc_tostring,
    libfunc_tostring,
    nil_tostring,
    undef_tostring
};

char *avm_tostring(struct avm_memcell *m) {
    assert(m->type >= 0 && m->type <= 7);
    return (*tostringFuncs[m->type])(m);
}



// ------------------- BOOLEAN


unsigned char number_tobool (struct avm_memcell *m) {return m->data.numVal != 0;}
unsigned char string_tobool (struct avm_memcell *m) {return m->data.strVal[0] != 0;}
unsigned char bool_tobool (struct avm_memcell *m) {return m->data.boolVal;}
unsigned char table_tobool (struct avm_memcell *m) {return 1;}
unsigned char userfunc_tobool (struct avm_memcell *m) {return 1;}
unsigned char libfunc_tobool (struct avm_memcell *m) {return 1;}
unsigned char nil_tobool (struct avm_memcell *m) {return 0;}
unsigned char undef_tobool (struct avm_memcell *m) {assert(0);return 0;}

tobool_func_t toboolFuncs[] = {
    number_tobool,
    string_tobool,
    bool_tobool,
    table_tobool,
    userfunc_tobool,
    libfunc_tobool,
    nil_tobool,
    undef_tobool
};

unsigned char avm_tobool(struct avm_memcell *m) {
    assert(m->type >= 0 && m->type < 7);
    return (*toboolFuncs[m->type])(m);
}

// ------------------- COMPARISON



// ---------------------------------------------------------------------------
// AVM
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    // display_instr();
    if (argc != 2) avm_error("Error getting binary from argument %s",argv[1]);
    bin_file_name = strdup(argv[1]);
    avm_initialize();
    while(!executionFinished) execution_cycle();
    if (warnings) printf("\n\033[0;32mExecutable '%s' returned with %u warning(s)!\033[0m\n\n",  argv[1], warnings);
    else printf("\n\033[0;32mExecutable '%s' returned succesfully!\033[0m\n\n",  argv[1]);
    return 0;
}

void avm_initialize (void) {
    warnings = 0;
    GlobalProgrammVarOffset = 0;
    if (!avmbinaryfile()) {
        fprintf(stderr,"\033[0;31mError initializing AVM\033[0m\n");
        exit(EXIT_FAILURE);
        return ;
    }
    avm_initstack();
    avm_register_libfuncs();
    top = N - GlobalProgrammVarOffset;
}

void avm_initstack(){
    for(unsigned i = 0 ; i<AVM_STACKSIZE ; ++i){
        AVM_WIPEOUT(stack[i]);
        stack[i].type = undef_m;
    }
}

// ------------------- CONSTS

char *consts_getstring(unsigned index) {
    return stringConsts[index];
}
double consts_getnumber(unsigned index) {
    return numConsts[index];
}

char *libfuncs_getused(unsigned index) {
    return namedLibFuncs[index];
}

// ------------------- LIBS
void avm_register_libfuncs() {
    library_func_t_addresses = (library_func_t *) malloc(sizeof(library_func_t) * totalNamedLibFuncs);
    avm_registerlibfunc("print", libfunc_print);
    avm_registerlibfunc("input", libfunc_input);
    avm_registerlibfunc("objectmemberkeys", libfunc_objectmemberkeys);
    avm_registerlibfunc("objecttotalmembers", libfunc_objecttotalmembers);
    avm_registerlibfunc("objectcopy", libfunc_objectcopy);
    avm_registerlibfunc("totalarguments", libfunc_totalarguments);
    avm_registerlibfunc("argument", libfunc_argument);
    avm_registerlibfunc("typeof", libfunc_typeof);
    avm_registerlibfunc("strtonum", libfunc_strtonum);
    avm_registerlibfunc("sqrt", libfunc_sqrt);
    avm_registerlibfunc("cos", libfunc_cos);
    avm_registerlibfunc("sin", libfunc_sin);
}

void libfunc_print(void) {
    unsigned n = avm_totalactuals();
    char *s;
    for (unsigned i = 0; i<n; i++) {
        s = avm_tostring(avm_getactual(i));
        printf("%s", s);
        free(s);
    }
    //printf("\n");
}

void libfunc_input(void) {
    unsigned n = avm_totalactuals();
    if (n) {
        avm_warning("'input()': no argument (not %d) expected!", n);
        retval.type = nil_m;
        return;
    }
    unsigned chunk = 128, current_size = chunk;
    char *buff = (char *) malloc(chunk);
    if(buff == NULL) {
        avm_warning("'input()': unable to allocate memory!", n);
        retval.type = nil_m;
        return;
    }
    int c = EOF;
    unsigned int i =0;
    while (( c = getchar() ) != '\n' && c != EOF) {
        buff[i++]=(char)c;
        if (i == current_size) {
            current_size = i + chunk;
            buff = realloc(buff, current_size);
        }
    }
    buff[i] = '\0';
    avm_memcellclear(&retval);

    // string = between double quotes
    if (buff[0] == '"' && buff[strlen(buff)] == '"') {
        retval.type = string_m;
        retval.data.strVal = buff;
        return;
    }

    // number = can be translated to number
    double number = atof(buff);
    if (number) {
        retval.type = number_m;
        retval.data.numVal = number;
        return;
    }

    // boolean = contains false/true
    if (strstr(buff, "false")) {
        retval.type = bool_m;
        retval.data.boolVal = 0;
        return;
    }
    if (strstr(buff, "true")) {
        retval.type = bool_m;
        retval.data.boolVal = 1;
        return;
    }

    // nil = contains nil
    if (strstr(buff, "nil")) {
        retval.type = nil_m;
        return;
    }

    char *tmp;
    for (unsigned i = 0; i<totalNamedLibFuncs; i++) {
        tmp = namedLibFuncs[i];
        if (!strcmp(tmp, buff)) {
            retval.type = libfunc_m;
            retval.data.libfuncVal = buff;
            return;
        }
    }

    for (unsigned i = 0; i<totalUserFuncs; i++) {
        tmp = userFuncs[i].id;
        if (!strcmp(tmp, buff)) {
            retval.type = userfunc_m;
            retval.data.funcVal = userFuncs[i].address;
            return;
        }
    }

    // string
    retval.type = string_m;
    retval.data.strVal = buff;
    return;
}

void libfunc_objectmemberkeys(void) {
    unsigned n = avm_totalactuals();
    if (n!=1) {
        avm_warning("'objectmemberkeys()': one argument (not %d) expected!", n);
        retval.type = nil_m;
        return;
    }
    struct avm_memcell *actual = avm_getactual(0);
    if (actual->type != table_m) {
        avm_warning("'objectmemberkeys()': table argument (not %s) expected!", typeStrings[actual->type]);
        retval.type = nil_m;
        return;
    }
    avm_memcellclear(&retval);
    retval.type = table_m;
    retval.data.tableVal = avm_tablenew();
    unsigned i = 0;
    struct avm_memcell index;
    index.type = number_m;
    index.data.numVal = 0;
    struct avm_table_bucket *bucket;
    for (int i=0; i<AVM_TABLE_HASHSIZE; i++) {
        bucket = actual->data.tableVal->numIndexed[i];
        while (bucket) {
            avm_tablesetelem(retval.data.tableVal, &index, &bucket->key);
            index.data.numVal++;
            bucket = bucket->next;
        }
        bucket = actual->data.tableVal->strIndexed[i];
        while (bucket) {
            avm_tablesetelem(retval.data.tableVal, &index, &bucket->key);
            index.data.numVal++;
            bucket = bucket->next;
        }
        bucket = actual->data.tableVal->boolIndexed[i];
        while (bucket) {
            avm_tablesetelem(retval.data.tableVal, &index, &bucket->key);
            index.data.numVal++;
            bucket = bucket->next;
        }
        bucket = actual->data.tableVal->ufncIndexed[i];
        while (bucket) {
            avm_tablesetelem(retval.data.tableVal, &index, &bucket->key);
            index.data.numVal++;
            bucket = bucket->next;
        }
        bucket = actual->data.tableVal->lfncIndexed[i];
        while (bucket) {
            avm_tablesetelem(retval.data.tableVal, &index, &bucket->key);
            index.data.numVal++;
            bucket = bucket->next;
        }
    }
}

void libfunc_objecttotalmembers(void) {
    unsigned n = avm_totalactuals();
    if (n!=1) {
        avm_warning("'objecttotalmembers()': one argument (not %d) expected!", n);
        retval.type = nil_m;
        return;
    }
    struct avm_memcell *actual = avm_getactual(0);
    if (actual->type != table_m) {
        avm_warning("'objecttotalmembers()': table argument (not %s) expected!", typeStrings[actual->type]);
        retval.type = nil_m;
        return;
    }
    avm_memcellclear(&retval);
    retval.type = number_m;
    retval.data.numVal = (double)actual->data.tableVal->total;
    return;
}

void libfunc_objectcopy(void) {
    unsigned n = avm_totalactuals();
    if (n!=1) {
        avm_warning("'objectcopy()': one argument (not %d) expected!", n);
        retval.type = nil_m;
        return;
    }
    struct avm_memcell *actual = avm_getactual(0);
    if (actual->type != table_m) {
        avm_warning("'objectcopy()': table argument (not %s) expected!", typeStrings[actual->type]);
        retval.type = nil_m;
        return;
    } 
    avm_memcellclear(&retval);
    retval.type = table_m;
    retval.data.tableVal = avm_tablenew();
    struct avm_table_bucket *bucket;
    for (int i=0; i<AVM_TABLE_HASHSIZE; i++) {
        bucket = actual->data.tableVal->numIndexed[i];
        while (bucket) {
            avm_tablesetelem(retval.data.tableVal, &bucket->key, &bucket->value);
            bucket = bucket->next;
        }
        bucket = actual->data.tableVal->strIndexed[i];
        while (bucket) {
            avm_tablesetelem(retval.data.tableVal, &bucket->key, &bucket->value);
            bucket = bucket->next;
        }
        bucket = actual->data.tableVal->boolIndexed[i];
        while (bucket) {
            avm_tablesetelem(retval.data.tableVal, &bucket->key, &bucket->value);
            bucket = bucket->next;
        }
        bucket = actual->data.tableVal->ufncIndexed[i];
        while (bucket) {
            avm_tablesetelem(retval.data.tableVal, &bucket->key, &bucket->value);
            bucket = bucket->next;
        }
        bucket = actual->data.tableVal->lfncIndexed[i];
        while (bucket) {
            avm_tablesetelem(retval.data.tableVal, &bucket->key, &bucket->value);
            bucket = bucket->next;
        }
    }
}

void libfunc_totalarguments(void) {
    unsigned p_topsp = avm_get_envvalue(topsp + AVM_SAVEDTOPSP_OFFSET);
    if (!p_topsp) {
        avm_warning("'totalarguments()': call outside a function!");
        retval.type = nil_m;
        return;
    }
    unsigned n = avm_totalactuals();
    if (n) {
        avm_warning("'totalarguments()': no argument (not %d) expected!", n);
        retval.type = nil_m;
        return;
    }
    avm_memcellclear(&retval);
    retval.type = number_m;
    retval.data.numVal = avm_get_envvalue(p_topsp + AVM_NUMACTUALS_OFFSET);
    return;
}

void libfunc_argument(void) {
    unsigned p_topsp = avm_get_envvalue(topsp + AVM_SAVEDTOPSP_OFFSET);
    if (!p_topsp) {
        avm_warning("'argument()': call outside of function!");
        retval.type = nil_m;
        return;
    }
    unsigned n = avm_totalactuals();
    if (n!=1) {
        avm_warning("'argument()': one argument (not %d) expected!", n);
        retval.type = nil_m;
        return;
    }
    struct avm_memcell *actual = avm_getactual(0);
    if (actual->type != number_m) {
        avm_warning("'argument()': number argument (not %s) expected!", typeStrings[actual->type]);
        retval.type = nil_m;
        return;
    } 
    avm_memcellclear(&retval);
    unsigned actuals = avm_get_envvalue(p_topsp + AVM_NUMACTUALS_OFFSET);
    if (actuals <= (unsigned)actual->data.numVal)  {
        avm_warning("'argument()': surrounding function has only %u arguments, not %u!", actuals, (unsigned)actual->data.numVal+1);
        retval.type = nil_m;
        return;
    }
    avm_memcell m = stack[p_topsp + AVM_STACKENV_SIZE + 1 + (unsigned)actual->data.numVal];
    retval.type = m.type;
    switch (m.type) {
        case number_m:
            retval.data.numVal = m.data.numVal;
            break;
        case string_m:
            retval.data.strVal = strdup(m.data.strVal);
            break;
        case bool_m:
            retval.data.boolVal = m.data.boolVal;
            break;
        case userfunc_m:
            retval.data.funcVal = m.data.funcVal;
            break;
        case libfunc_m:
            retval.data.libfuncVal = strdup(m.data.libfuncVal);
            break;
        case table_m:
            retval.data.tableVal = m.data.tableVal;
            break;
        case nil_m:
        case undef_m:
            break;
    }
    return;
}

void libfunc_typeof(void) {
    unsigned n = avm_totalactuals();
    if (n!=1) {
        avm_warning("'typeof()': one argument (not %d) expected!", n);
        retval.type = nil_m;
        return;
    }
    avm_memcellclear(&retval);
    retval.type = string_m;
    retval.data.strVal = strdup(typeStrings[avm_getactual(0)->type]);
}

void libfunc_strtonum(void) {
    unsigned n = avm_totalactuals();
    if (n!=1) {
        avm_warning("'strtonum()': one argument (not %d) expected!", n);
        retval.type = nil_m;
        return;
    }
    struct avm_memcell *actual = avm_getactual(0);
    if (actual->type != string_m) {
        avm_warning("'strtonum()': string argument (not %s) expected!", typeStrings[actual->type]);
        retval.type = nil_m;
        return;
    }
    avm_memcellclear(&retval);
    retval.type = number_m;
    retval.data.numVal = atof(actual->data.strVal);
    return;
}

void libfunc_sqrt(void) {
    unsigned n = avm_totalactuals();
    if (n!=1) {
        avm_warning("'sqrt()': one argument (not %d) expected!", n);
        retval.type = nil_m;
        return;
    }
    struct avm_memcell *actual = avm_getactual(0);
    if (actual->type != number_m) {
        avm_warning("'sqrt()': number argument (not %s) expected!", typeStrings[actual->type]);
        retval.type = nil_m;
        return;
    }
    avm_memcellclear(&retval);
    retval.type = number_m;
    retval.data.numVal = sqrt(actual->data.numVal);
}

void libfunc_cos(void) {
    unsigned n = avm_totalactuals();
    if (n!=1) {
        avm_warning("'cos()': one argument (not %d) expected!", n);
        retval.type = nil_m;
        return;
    }
    struct avm_memcell *actual = avm_getactual(0);
    if (actual->type != number_m) {
        avm_warning("'cos()': number argument (not %s) expected!", typeStrings[actual->type]);
        retval.type = nil_m;
        return;
    }
    avm_memcellclear(&retval);
    retval.type = number_m;
    retval.data.numVal = cos(actual->data.numVal);
}

void libfunc_sin(void) {
    unsigned n = avm_totalactuals();
    if (n!=1) {
        avm_warning("'sin()': one argument (not %d) expected!", n);
        retval.type = nil_m;
        return;
    }
    struct avm_memcell *actual = avm_getactual(0);
    if (actual->type != number_m) {
        avm_warning("'sin()': number argument (not %s) expected!", typeStrings[actual->type]);
        retval.type = nil_m;
        return;
    }
    avm_memcellclear(&retval);
    retval.type = number_m;
    retval.data.numVal = sin(actual->data.numVal);
}

// ------------------- DISPLAY

void print_stack() {
    
    int i = AVM_STACKSIZE;
    printf("====== STACK ====== \n");
    
    while (--i) {

        printf("Cell:%d type:%d", i , stack[i].type);
        printf("\n");
        if (i == 4080) break;
    }

}

void use_instr_result(struct vmarg result){
    struct userfunc* f1= NULL;
    if(result.type == userfunc_a){
            f1 = &userFuncs[result.val];
    }
    switch (result.type) {
            case empty_a:break;
            case label_a:
                printf("00_%u ", result.val);
                break;
            case global_a:
                printf("01_%u ", result.val);
                break;
            case formal_a:
                printf("02_%u ", result.val);
                break;
            case local_a:
                printf("03_%u ", result.val);
                break;
            case number_a:
                printf("04_%u_[%f] ", result.val,numConsts[result.val]);
                break;
            case string_a:
                 printf("05_%u_[\"%s\"] ", result.val,stringConsts[result.val]);
                break;
            case bool_a:
                printf("06_%u ", result.val);
                break;
            case nil_a:
                printf("07_nill");
                break;
            case userfunc_a:
                printf("08_%u_[%s] ", result.val,(f1->id));
                break;
            case libfunc_a:
                printf("09_%u_[%s] ", result.val,namedLibFuncs[result.val]);
                break;
            case retval_a:
                printf("10_(retval) ", result.val);
                break;
            default:
                assert(0);
        }
}

void use_instr_arg1(struct vmarg arg1){
    struct userfunc* f2= NULL;
    if(arg1.type == userfunc_a){
            f2 = &userFuncs[arg1.val];
    }
    switch (arg1.type) {
            case empty_a:break;
            case label_a:
                printf("00_%u ", arg1.val);
                break;
            case global_a:
                printf("01_%u ", arg1.val);
                break;
            case formal_a:
                printf("02_%u ", arg1.val);
                break;
            case local_a:
                printf("03_%u ", arg1.val);
                break;
            case number_a:
                printf("04_%u_[%f] ", arg1.val, numConsts[arg1.val]);
                break;
            case string_a:
                 printf("05_%u_[\"%s\"] ", arg1.val, stringConsts[arg1.val]);
                break;
            case bool_a:
                printf("06_%u ", arg1.val);
                break;
            case nil_a:
                printf("07_nill");
                break;
            case userfunc_a:
                printf("08_%u_[%s] ", arg1.val, f2->id);
                break;
            case libfunc_a:
                printf("09_%u_[%s] ", arg1.val, namedLibFuncs[arg1.val]);
                break;
            case retval_a:
                printf("10_(retval) ", arg1.val);
                break;
            default:
                assert(0);
        }
}

void use_instr_arg2( struct vmarg arg2){
    struct userfunc* f3= NULL;
    if(arg2.type == userfunc_a){
            f3 = &userFuncs[arg2.val];
    }
    switch (arg2.type) {
            case empty_a:break;
            case label_a:
                printf("00_%u ", arg2.val);
                break;
            case global_a:
                printf("01_%u ", arg2.val);
                break;
            case formal_a:
                printf("02_%u ", arg2.val);
                break;
            case local_a:
                printf("03_%u ", arg2.val);
                break;
            case number_a:
                printf("04_%u_[%f] ", arg2.val, numConsts[arg2.val]);
                break;
            case string_a:
                 printf("05_%u_[\"%s\"] ", arg2.val, stringConsts[arg2.val]);
                break;
            case bool_a:
                printf("06_%u ", arg2.val);
                break;
            case nil_a:
                printf("07_nill");
                break;
            case userfunc_a:
                printf("08_%u_[%s] ", arg2.val, (f3->id));
                break;
            case libfunc_a:
                printf("09_%u_[%s] ", arg2.val, namedLibFuncs[arg2.val]);
                break;
            case retval_a:
                printf("10_(retval) ", arg2.val);
                break;
            default:
                assert(0);
        }
}
