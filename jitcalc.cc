#include <iostream>
#include <string>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <map>
#include <vector>
#include <cassert>
#include <getopt.h>
#include "sljit_src/sljitLir.h"

enum {
    TK_NUMBER,
    TK_ADD,TK_SUB,TK_MUL,TK_DIV,
    TK_VARIABLE,
    TK_UNKNOWN,
    TK_LPAR,TK_RPAR,
    TK_EOF
};

static
std::map<std::string,int> STRING_POOL;

static
std::vector<int> VALUE_POOL;

static
int STRING_INDEX = 0;

class tokenizer {
public:
    tokenizer( const char* s ):
        source_(s),
        pos_(0)
    { tk_ = next(); }
    int next() {
        do {
            switch( source_[pos_] ) {
                    case 0: return (tk_=TK_EOF);
                case ' ':case '\t':case '\r':case '\n':
                        ++pos_;
                        continue;
                case '1':case '2':case '3':case '4':case '5':
                case '6':case '7':case '8':case '9':case '0':
                        return (tk_=TK_NUMBER);
                case '(': return (tk_=TK_LPAR);
                case ')': return (tk_=TK_RPAR);
                case '+': return (tk_=TK_ADD);
                case '-': return (tk_=TK_SUB);
                case '*': return (tk_=TK_MUL);
                case '/': return (tk_=TK_DIV);
                default:
                    if( std::isalpha(source_[pos_]) ) {
                        return (tk_=TK_VARIABLE);
                    } else {
                        return (tk_=TK_UNKNOWN);
                    }
            }
        } while(true);
        return -1;
    }

    int move( int offset ) {
        pos_ += offset;
        return next();
    }

    int set_pos( int pos ) {
        pos_ = pos;
        return next();
    }

    int pos() const {
        return pos_;
    }

    const char* source() const {
        return source_;
    }

    int token() const {
        return tk_;
    }

private:
    const char* source_;
    int pos_;
    int tk_;
};

bool parse_factor( tokenizer* tk , bool side , sljit_compiler* compiler );

sljit_compiler* parse_term( tokenizer* tk , sljit_compiler* compiler ) {
    if( !parse_factor(tk,false,compiler) ) 
        return NULL;
    do {
        int op = tk->token();
        switch(op) {
            case TK_ADD: 
            case TK_SUB:
                tk->move(1);
                break;
            default:
                // Emit return value here
                return compiler;
        }
        if( !parse_factor(tk,true,compiler) )
            return NULL;

        if( op == TK_ADD ) {
            sljit_emit_op2( compiler , SLJIT_ADD , 
                    SLJIT_S0 , 0 ,
                    SLJIT_S0 , 0 ,
                    SLJIT_S1 , 0 );
        } else {
            sljit_emit_op2( compiler , SLJIT_SUB , 
                    SLJIT_S0 , 0 ,
                    SLJIT_S0 , 0 ,
                    SLJIT_S1 , 0 );
        }

    } while(true);
    return compiler;
}

sljit_compiler* parse( tokenizer* tk ) {
    sljit_compiler* compiler = sljit_create_compiler();
    sljit_emit_enter(compiler,0,3,3,3,0,0,0);
    return parse_term(tk,compiler);
}

// -1 error
// 1 number
// 2 variable
// 3 sub expression and code has been generated
int parse_atomic( tokenizer* tk , 
                  sljit_compiler* c , bool side,
                  std::string* variable , 
                  int* number ) {
    if( tk->token() == TK_NUMBER ) {
        char* end;
        errno = 0;
        int val = std::strtol( tk->source() + tk->pos() ,
                &end,10);
        if( errno != 0 )
            return -1;
        *number = val;
        tk->set_pos( end - tk->source() );
        return 1;
    } else if (tk->token() == TK_VARIABLE ) {
        variable->clear();
        int i = 0;
        for( i = tk->pos() ; std::isalpha( tk->source()[i] ) ; ++i )
            variable->push_back(tk->source()[i]);
        tk->set_pos(i);
        return 2;
    } else {
        if( tk->token() == TK_LPAR ) {
            tk->move(1);
            if( parse_term(tk,c) < 0 )
                return -1;
            if( side ) {
                // Move the term value into R0 for factor 
                sljit_emit_op1( c , SLJIT_MOV ,
                        SLJIT_R1 , 0,
                        SLJIT_S0 , 0 );
            } else {
                // Move the term value into R0 for factor 
                sljit_emit_op1( c , SLJIT_MOV ,
                        SLJIT_R0 , 0,
                        SLJIT_S0 , 0 );
            }
            if( tk->token() != TK_RPAR ) {
                return -1;
            } else {
                tk->move(1);
            }
            return 3;
        }
        return -1;
    }
}

int SLJIT_CALL lookup( sljit_sw idx ) {
    return VALUE_POOL[idx];
}

bool parse_factor( tokenizer* tk , bool side , sljit_compiler* compiler ) {
    std::string var;
    int var_idx;
    int num;
    int ret = parse_atomic(tk,compiler,false,&var,&num);
    if( ret < 0 )
        return false;

    // If it is a variable lookup , convert the string name into the id
    if( ret == 2 ) {
        if( STRING_POOL.find( var ) == STRING_POOL.end() ) 
            return false;
        var_idx = STRING_POOL[var];
    }
    // Generate code for looking up the value in the global table here
    if( ret == 1 ) {
        sljit_emit_op1(compiler,
                SLJIT_MOV, SLJIT_R0 , 0,
                SLJIT_IMM,num);
    } else if( ret == 3 ) {
        // The output is in R0, we are safe to do NOTHING here
    } else {
        sljit_emit_op1(compiler,
                SLJIT_MOV, 
                SLJIT_R0, 0, 
                SLJIT_IMM,var_idx);
        // Call the function here
        sljit_emit_ijump( compiler , SLJIT_CALL1 , SLJIT_IMM ,
                SLJIT_FUNC_OFFSET(lookup));

        // Move the output to the S0 register
        sljit_emit_op1( compiler , SLJIT_MOV , SLJIT_R0 , 0 ,
                SLJIT_RETURN_REG, 0 );
        
    }


    do {
        int op = tk->token();
        switch(op) {
            case TK_MUL:
            case TK_DIV:
                tk->move(1);
                break;
            default: 
                goto done;
        }

        // Parse the right hand side value
        ret = parse_atomic(tk,compiler,true,&var,&num);
        if( ret < 0 )
            return false;

        if( ret == 1 ) {

            sljit_emit_op1(
                    compiler,
                    SLJIT_MOV, 
                    SLJIT_R1 , 
                    0,
                    SLJIT_IMM,
                    num);

            if( op == TK_MUL ) {
                sljit_emit_op0( compiler , SLJIT_LSMUL );
            } else {
                sljit_emit_op0( compiler , SLJIT_LSDIV );
            }

        } else if(ret == 3) {
            if( op == TK_MUL ) {
                sljit_emit_op0( compiler , SLJIT_LSMUL );
            } else {
                sljit_emit_op0( compiler , SLJIT_LSDIV );
            }
        } else {
            // Generate lookup shit here
            if( STRING_POOL.find( var ) == STRING_POOL.end() ) 
                return false;
                var_idx = STRING_POOL[var];

            // Save temporary value in R0 to S1 to make our life easier
            // S1 will be maintained cross calling boundary
            sljit_emit_op1(compiler,
                    SLJIT_MOV,
                    SLJIT_S1 , 0 ,
                    SLJIT_R0 , 0 );

            sljit_emit_op1(compiler,
                SLJIT_MOV, 
                SLJIT_R0, 0, 
                SLJIT_IMM,var_idx);

            // Call the function here
            sljit_emit_ijump( compiler , SLJIT_CALL1 , SLJIT_IMM ,
                    SLJIT_FUNC_OFFSET(lookup));

            // Move the output to the S0 register
            sljit_emit_op1( compiler , SLJIT_MOV , SLJIT_R0 , 0 ,
                    SLJIT_RETURN_REG, 0 );

            // Move the value in S1 to R1 
            sljit_emit_op1( compiler , SLJIT_MOV , SLJIT_R1 , 0,
                    SLJIT_S1 , 0 );

            if( op == TK_MUL ) {
                sljit_emit_op0( compiler , SLJIT_LSMUL );
            } else {
                sljit_emit_op0( compiler , SLJIT_LSDIV );
            }
        }
    } while(true);

done:
    if( side ) {
        // Right hand side, move the value to S1 
        sljit_emit_op1( compiler , SLJIT_MOV,
                SLJIT_S1 , 0,
                SLJIT_R0 , 0 );
    } else {
        sljit_emit_op1( compiler , SLJIT_MOV,
                SLJIT_S0 , 0,
                SLJIT_R0 , 0 );
    }
    return true;
}

void* compile( const char* src ) {
    tokenizer tk(src);
    sljit_compiler* c  = parse(&tk);
    if(c == NULL) return NULL;
    // Before that we need to move the result from S0 to return
    sljit_emit_return( c , SLJIT_MOV, SLJIT_S0 , 0);
    return sljit_generate_code(c);
}

typedef int (*jit_main)();

struct option OPTIONS[] = {
    {"expr",required_argument,0,'e'},
    {"var",optional_argument,0,'a'}
};

int main( int argc , char* argv [] ) {
    int opt_idx = 0;
    int c;
    const char* expr = NULL;
    while((c=getopt_long(argc,argv,"e:v:",OPTIONS,&opt_idx)) != -1) {
        switch(c) {
            case 'e': 
                expr = optarg;
                break;
            case 'v': {
                std::string v(optarg);
                std::size_t pos;
                if( (pos = v.find_first_of("=")) == std::string::npos ) { 
                    break;
                }
                STRING_POOL.insert(std::make_pair(
                            v.substr(0,pos),STRING_INDEX++));
                VALUE_POOL.push_back(atoi(v.substr(pos+1,v.size()-pos).c_str()));
                break;
            }
            default:
                std::cerr<<"Unknown parameters!"<<std::endl;
                return -1;
        }
    }
    if( expr == NULL ) {
        std::cerr<<"No expression is given!"<<std::endl;
        return -1;
    }
    void* compiled_code = compile(expr);
    if(compiled_code == NULL) {
        std::cerr<<"Error for compilation!"<<std::endl;
        return -1;
    }
    jit_main m = (jit_main)(compiled_code);
    std::cout<<m()<<std::endl;
    return 0;
}

