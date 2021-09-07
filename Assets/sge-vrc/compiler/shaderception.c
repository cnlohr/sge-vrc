#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define TCC_CRASH_IMPLEMENTATION
#include "cncrashhandler_mini.h"

// 1 = whitespace
// 2 = numeric
// 3 = alpha
// 4 = operator
// 5 = semicolon
// 6 = '='
const uint8_t toktypes[128] = {
//  0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f 
	0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 4, 0, 0, 0, 0, 0, 0,10,11,21,22,12, 4, 2, 4,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 5, 4, 6, 4, 0,
	0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 3,
	0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,13, 0,14, 0, 0,
};

enum tokentype
{
	TOK_INVALID = 0,
	TOK_WHITE,
	TOK_NUMERIC,
	TOK_ALPHA,
	TOK_OP,
	TOK_SEMI,
	TOK_EQALS,
};


const char * filename;
struct tokes
{
	char * text;
	int lineno;
	int charno;
	int type;
} * tokens;
int tokenno;
int numtoks;
int labelCount = 0;

FILE * f;

char ** compiled;
int num_compiled_symbols;
char ** linked;
int num_linked_symbols;


void Emit( const char * command, ... );
char * Peek( );
char * PeekNext( );
char * Advance( );
char * EatIdent( );
int IsAssignment();
int Match( const char * toks );
int MatchNext( const char * toks );
int MatchTo( int advance, const char * toks );
enum tokentype GetTypeTo( int advance );

int Eat( const char * toks );
int IsAtEnd();
enum tokentype GetType();
void DieAtToken( const char * errmsg );

void Assignment();
void Conditional( int endlabel );
void ForLoop();
void FuncDef();
void WhileLoop();
void Expression();
void BoolOp();
void Comparison();
void AddSub();
void DivMul();
void Term();
void Block();
void FuncCall();

void Link();
void Inline();


int numFuncs;
char ** funcIdents;
char *** funcParams;
int currentFunc;
int currentParsed;
int prevParsed;




////////////// TOKEN PARSING /////////////////

int MatchNext( const char * t2 )
{
	return MatchTo( 1, t2 );
}

int MatchTo( int advance, const char * toks )
{
	//printf( "TM: %d %d %d // %s %s\n", tokenno, advance, numtoks, tokens[tokenno+advance].text, toks );
	if( tokenno + advance >= numtoks )
	{
		return 0;
	}
	else
	{
		return strcmp( tokens[tokenno+advance].text, toks ) == 0;
	}
}

enum tokentype GetTypeTo( int advance )
{
	if( tokenno + advance >= numtoks )
	{
		return 0;
	}
	else
	{
		return tokens[tokenno + advance].type;
	}
}


int IsAssignment()
{
	if( Match( "let" ) || Match( "set" ) ) return 1; // normal assignments

	if( GetType() == TOK_ALPHA )
	{
		// Handle swizzle
		int offset = 1;
		if( MatchNext( "." ) && GetTypeTo( 2 ) == TOK_ALPHA )
		{
			offset += 2;
		}

		// Assignment types
		if ( MatchTo( offset, "=" ) && !MatchTo( offset+1, "=" ) ) return 1;
		if ( MatchTo( offset, "+" ) &&  MatchTo( offset+1, "=" ) ) return 1;
		if ( MatchTo( offset, "-" ) &&  MatchTo( offset+1, "=" ) ) return 1;
		if ( MatchTo( offset, "*" ) &&  MatchTo( offset+1, "=" ) ) return 1;
		if ( MatchTo( offset, "/" ) &&  MatchTo( offset+1, "=" ) ) return 1;
		if ( MatchTo( offset, "+" ) &&  MatchTo( offset+1, "+" ) ) return 1;
		if ( MatchTo( offset, "-" ) &&  MatchTo( offset+1, "-" ) ) return 1;
	}
	return 0;
}



int Match( const char * match )
{
	return MatchTo( 0, match );
}

int Eat( const char * toks )
{
	if( !IsAtEnd() && strcmp( tokens[tokenno].text, toks ) == 0 )
	{
		tokenno++;
	}
	else
	{
		char diemsg[1024];
		sprintf( diemsg, "Could not find correct token (%s not %s)", IsAtEnd()?"EOF":tokens[tokenno].text, toks );
		DieAtToken( diemsg );
	}
}

char * Peek( )
{
	if( IsAtEnd() )
	{
		return 0;
	}
	else
	{
		return tokens[tokenno].text;
	}
}

char * PeekNext( )
{
	if( tokenno + 1 < numtoks )
	{
		return tokens[tokenno + 1].text;
	}
	else
	{
		return 0;
	}
}

char * EatIdent()
{
	if( GetType() != TOK_ALPHA )
	{
		DieAtToken( "Expected function call identifier" );
	}
	return Advance();
}
	
char * Advance( )
{
	char * ret = Peek();
	tokenno++;
	return ret;
}

int IsAtEnd()
{
	return tokenno >= numtoks;
}

enum toktypes GetType()
{
	return GetTypeTo( 0 );
}

void DieAtToken( const char * errmsg )
{
	if( IsAtEnd() )
	{
		fprintf( stderr, "Error: %s: %s\n", errmsg, filename );
	}
	else
	{
		fprintf( stderr, "Error: %s (%s)(%d) at %s:%d:%d\n", errmsg, tokens[tokenno].text, tokens[tokenno].type, filename, tokens[tokenno].lineno, tokens[tokenno].charno );
	}
	exit( 0 );
}


////////////// FUNCTION SWITCHING /////////////////



void SwitchToFunction( char * ident, char ** parameters, int numpar )
{
	int func = numFuncs++;
	funcIdents = realloc( funcIdents, numFuncs * sizeof( char * ) );
	funcParams = realloc( funcParams, numFuncs * sizeof( char ** ) );
	funcIdents[func] = ident;
	funcParams[func] = parameters;

	if( currentFunc == 0)
	{
		prevParsed = currentParsed;
	}
	currentFunc = func;
	currentParsed = 0;
}

void SwitchToGlobal()
{
	currentParsed = prevParsed;
	currentFunc = 0;
}

///////////////////////// COMPILATION ////////////////////////////

void Emit( const char * command, ... )
{
	va_list argp;
	va_start( argp, command );
	vprintf( command, argp );
	putchar( '\n' );
	compiled = realloc( compiled, sizeof(char*)*(num_compiled_symbols+1) );
	compiled[num_compiled_symbols++] = strdup( command );
}




void WhileLoop()
{
	int startid = labelCount++;
	int endid = labelCount++;

	Eat( "while" );
	Eat( "(" );

	Emit( "LABEL start_%d", startid );
	Expression(); // condition
	Emit( "CONDJUMP end_%d", endid );

	Eat( ")" );
	Eat( "{" );

	Block( );

	Emit( "JUMP end_%d", endid);

	Eat( "}" );

	Emit( "LABEL end_%d", endid);
}


void ForLoop()
{
	int checkLabel = labelCount++;
	int loopLabel = labelCount++;
	int incrementLabel = labelCount++;
	int endLabel = labelCount++;

	Eat( "for" );
	Eat( "(" );
	Assignment(); // init
	Eat( ";" );

	Emit( "LABEL check_%d", checkLabel );
	Expression(); // condition
	Eat( ";");

	Emit( "CONDJUMP end_%d", endLabel );
	Emit( "JUMP loop_%d", loopLabel );
	Emit( "LABEL increment_%d", incrementLabel );

	Assignment(); // increment

	Eat( ")" );

	Emit( "JUMP check_%d", checkLabel );
	Emit( "LABEL loop_%d", loopLabel );

	Eat( "{" );

	Block();
	Emit( "JUMP increment_%d", incrementLabel );

	Eat( "}" );

	Emit( "LABEL end_%d", endLabel );
}





void FuncDef()
{
	char ** parameters;
	Eat( "fun" );
	char * ident = EatIdent();
	Eat( "(" );
printf( "1" );
	int arity = 0;
	if( GetType() == TOK_ALPHA ) // has parameters
	{
		parameters = realloc( parameters, sizeof( char * ) * ( arity + 1 ) );
		parameters[arity++] = EatIdent();
		while ( Match( "," ) )
		{
			Eat( "," );
			parameters[arity++] = EatIdent();
		}
	}

	Eat( ")" );
	Eat( "{" );
printf( "2" );

	// emit body into correct store
	SwitchToFunction( ident, parameters, arity );
printf( "3" );

	// pop parameters from stack in reverse order and put in registers
	for (int i = 0; i < arity; i++)
	{
		Emit( "SETVAR %s", parameters[arity-1-i]);
	}
printf( "4" );

	// body
	Block();
printf( "5" );
	// restore global store
	SwitchToGlobal();
printf( "6" );

	Eat("}");
}

void FuncCall()
{
	char * ident = EatIdent();
	Eat( "(" );
	Expression();
	while( Match( "," ) )
	{
		Eat( "," );
		Expression();
	}
	Eat( ")" );
	Emit( "CALL %s", ident );
}

void Conditional( int endlabel )
{
	Eat( "if" );
	Eat( "(" );

	Expression(); // condition

	Eat( ")" );
	Eat( "{" );
	
	int falselabel = labelCount++;

	Emit("CONDJUMP false_%d", falselabel);

	Block();

	Emit( "JUMP end_%d", endlabel);
	Emit( "LABEL false_%d", falselabel);

	Eat( "}" );

	if( Match( "else" ) )
	{
		Eat( "else" );
		if( Match( "if" ) ) // else if
		{
			Conditional( endlabel );
		}
		else // else
		{
			Eat( "{" );
			Block();
			Eat( "}" );
			Emit("LABEL end_%d", endlabel);
		}
	}
	else
	{
		Emit("LABEL end_%d", endlabel);
	}
}


void Assignment()
{
	if (Match("let") || Match("set"))
	{
		Advance();
	}

	if( GetType() != TOK_ALPHA )
	{
		DieAtToken( "Expected identifier" );
	}
	
	char * ident = Advance();
	char * swizzle = 0;
	char * identFirst = ident;

	if ( Match( "." ) ) // handle swizzle
	{
		Eat( "." );
		if( GetType() != TOK_ALPHA )
		{
			DieAtToken( "Expected swizzle" );
		}
		swizzle = Advance();
	}
	
	if ( Match( "=") ) // normal assignment
	{
		Eat( "=" );
		Expression();
	}
	else if ( MatchNext("=") && (Match("+") || Match("-") || Match("*") || Match("/"))) // math assignment
	{
		Emit("PUSHVAR %s", identFirst);

		char * op = Advance();
		Eat( "=" );
		Expression();

		Emit( "BINOP %s", op );
	}
	else if ( ( Match("+") && MatchNext("+")) || (Match("-") && MatchNext("-"))) // ++, --
	{
		Emit( "PUSHVAR %s", ident );
		char * op = Advance();
		Advance();
		Emit( "PUSHCONST (%f, %f, %f, %f)", 1, NAN, NAN, NAN );
		Emit( "BINOP %s", op);
	}
	else
	{
		DieAtToken("Unknown type of assignment.");
	}

	if( swizzle )
		Emit( "SETVAR %s.%s", ident, swizzle);
	else
		Emit( "SETVAR %s", ident );
}


int Statement()
{
	printf( "STATEMENT %s\n", Peek() );
	if( Match( "set" ) || Match( "let" ) )
	{
		Assignment();
		Eat( ";" );
	}
	else if( Match( "if" ) )
		Conditional( labelCount++ );
	else if( Match( "while" ) )
		WhileLoop(); 
	else if( Match( "for" ) )
		ForLoop();
	else if( Match( "fun" ) )
		FuncDef();
	else if( IsAssignment() )
	{
		Assignment();
		Eat( ";" );
	}
	else
		return 1;
	return 0;
}

void Expression()
{
	BoolOp();
}

void BoolOp()
{
	Comparison();

	while( 1 )
	{
		if( ( Match( "&&" ) ) || ( Match( "||" ) ) )
		{
			const char * name1 = Advance();
			Comparison();
			Emit( "BINOP %s", name1 );
			if( IsAtEnd() ) return;
		}
		else
		{
			return;
		}
	}
}

void Comparison()
{
	AddSub();

	while( 1 )
	{
		if( Match( "<" ) || Match( "==" ) || Match( ">" ) )
		{
			if( !Match( "==" ) && MatchNext( "=" ) )
			{
				const char * name1 = Advance();
				const char * name2 = Advance();
				AddSub();
				Emit( "BINOP %s%s", name1, name2 );	 //Glue tokens.		
			}
			else
			{
				const char * name1 = Advance();
				AddSub();
				Emit( "BINOP %s", name1 );			
			}
		}
		else
		{
			return;
		}
	}
}



void AddSub()
{
	DivMul();

	while( Match( "+" ) || Match( "-" ) )
	{
		const char * adv = Advance();
		DivMul();
		Emit( "BINOP %s", adv );
		if ( IsAtEnd() ) return;
	}
}

void DivMul()
{
	Term();

	while( Match( "*" ) || Match( "/" ) )
	{
		const char * tok = Advance();
		Term();
		Emit( "BINOP %s", tok );
		if ( IsAtEnd() ) return;
	}
}

void Term()
{
	if( IsAtEnd() ) return;
	if( GetType() == TOK_ALPHA )
	{
		// Identifier
		if( MatchNext( "(" ) )
		{
			// Function call.
			FuncCall();
		}
		else
		{
			// Regular identifier.
			Emit("PUSHVAR %s", Advance());
		}
	}
	else if( GetType() == TOK_NUMERIC ) //Number
	{
		Emit( "PUSHCONST (%f, %f, %f, %f)", atof( Advance() ), NAN, NAN, NAN );
	}
	else if( Match( "(" ) )
	{
		// Grouped.
		Eat( "(" );
		Expression();
		Eat( ")" );
	}
	else if( Match( "+" ) || Match( "-" ) )
	{
		// Unary.
		char * text = Advance();
		Term();
		Emit( "UNOP %s", text );
	}

	// Swizzling
	if ( Match(".") )
	{
		Eat(".");

		char * swizzle = EatIdent();
		float res[4] = { 0 };
		for (int i = 0; i < 4; i++)
		{
			switch (swizzle[i])
			{
				case 0: i = 4; break;
				case 'x': case 'r': res[i] = 1; break;
				case 'y': case 'g': res[i] = 2; break;
				case 'z': case 'b': res[i] = 3; break;
				case 'w': case 'a': res[i] = 4; break;
				default:
				{
					DieAtToken( "Unkown swizzle." );
				}
			}
		}

		Emit( "PUSHCONST (%f, %f, %f, %f)", res[0], res[1], res[2], res[3] );
		Emit( "CALL swizzle");
	}
}

void Block()
{
	printf( "\n" );
	// A block is a list of Statements.
	// Each statement is a line of the block.
	// A statmenet does not produce a value.
	while (!IsAtEnd() && !Statement());
	printf( "---" );


	// optional return statement
	int hasReturn = Match("return");
	if (hasReturn) Advance();
	// final expression is evaluated
	Expression();
	// optional semicolon for return
	if (hasReturn) Eat( ";" );

	if (!IsAtEnd() && !Match("}")) DieAtToken("End of block reached, but there is more code.");
}


////////// LINKING //////////////////


void Inline()
{
/*	
	char ** compiled;
int num_compiled_symbols;
char ** linked;
int num_linked_symbols;



	for (int i = 0; i < parsed[id].Length; i += 2)
	{
		if (parsed[id][i] == null) break;

		// User function, inline
		if (parsed[id][i] == "CALL" && FuncIdentToIndex((string)parsed[id][i+1]) == 0)
		{
			for (int j = 0; j < funcIdents.Length; j++) // find body of function to inline
			{
				if (funcIdents[j] == null) break;

				if (funcIdents[j].Equals(parsed[id][i+1]))
				{
					// store previous renaming table
					string[] prevRenameFrom = null;
					if (renameFrom != null)
					{
						prevRenameFrom = new string[renameFrom.Length];
						System.Array.Copy(renameFrom, prevRenameFrom, renameFrom.Length);
					}

					// setup renaming table
					renameFrom = funcParams[j];
					renameTo = new string[renameFrom.Length];
					for (int k = 0; k < renameTo.Length; k++)
					{
						if (renameFrom[k] == null) break;
						renameTo[k] = "_reg" + regCount++;
					}

					// recursively inline function
					Inline(j);

					// restore renaming table
					renameFrom = prevRenameFrom;
					break;
				}
			}
		}
		else
		{
			// Rename variables if a mapping table is available
			if (parsed[id][i] == "PUSHVAR" || parsed[id][i] == "SETVAR")
			{
				string ident = (string)parsed[id][i+1];
				if (renameFrom != null)
				{
					for (int j = 0; j < renameFrom.Length; j++)
					{
						if (ident == renameFrom[j])
						{
							parsed[id][i+1] = renameTo[j];
							break;
						}
					}
				}
			}
			// Dont add labels
			if (parsed[id][i] == "LABEL")
			{
				labels[currentLabels++] = parsed[id][i+1];
				labels[currentLabels++] = currentLinked;
			}
			else
			{
				linked[currentLinked++] = parsed[id][i];
				linked[currentLinked++] = parsed[id][i+1];
			}
		}
	}
	*/
}

void Link()
{
	/*
	renameFrom = null;
	renameTo = null;

	// Inlining, start with global scope
	Inline(0);

	// Jump location linking
	for (int i = 0; i < linked.Length; i += 2)
	{
		if (linked[i] == null) break;

		if (linked[i] == "JUMP" || linked[i] == "CONDJUMP")
		{
			string label = (string)linked[i+1];
			for (int j = 0; j < labels.Length; j += 2)
			{
				if (labels[j] == label)
				{
					linked[i+1] = (float)labels[j+1];
				}
			}
		}
	}

	// Register allocation
	bool[] alloced = new bool[linked.Length];
	int regAlloc = 0;
	for (int i = 0; i < linked.Length; i += 2)
	{
		if (linked[i] == null) break;

		if (linked[i] == "PUSHVAR" || linked[i] == "SETVAR")
		{
			if (alloced[i]) // don't allocate registers multiple times
				continue;

			string reg = (regAlloc++).ToString();
			string[] a = linked[i+1].ToString().Split('.');

			for (int j = 0; j < linked.Length; j += 2)
			{
				if (linked[j] == null) break;

				string[] b = linked[j+1].ToString().Split('.');

				if ((linked[j] == "PUSHVAR" || linked[j] == "SETVAR") && a[0] == b[0])
				{
					if (b.Length > 1) // handle swizzle
					{
						linked[j+1] = reg + "." + b[1];
					}
					else
					{
						linked[j+1] = reg;
					}
					alloced[j] = true;
				}
			}
		}
	}
	*/
}


int main( int argc, char ** argv )
{
	tcccrash_install();

	if( argc < 2 )
	{
		fprintf( stderr, "Error: Usage: shaderception [testfile.psl]\n" );
		return -5;
	}
	filename = argv[1];

	{
		FILE * f = fopen( filename, "rb" );

		int c;
		int charno = 0;
		int lineno = 0;
		int currenttokenype = 1;
		int tokpl = 0;

		tokens = malloc( (tokenno+1) * sizeof( struct tokes ) );

		while( ( c = fgetc( f ) ) != EOF )
		{
			int tt = ( c >= sizeof( toktypes ) )?0:toktypes[c];
			if( tt == 0 )
			{
				fprintf( stderr, "Error: Token failure: %s:%d:%d (%d)\n", filename, lineno, charno, c );
			}
			charno++;
			if( c == '\n' ) { lineno++; charno = 0; }
			tokens[tokenno].text = realloc( tokens[tokenno].text, tokpl+2 );
			tokens[tokenno].text[tokpl++] = c;
			tokens[tokenno].text[tokpl] = 0;

			if( ( currenttokenype != tt && 
				!( currenttokenype == 3 && tt == 2 ) ) // Allow identifiers to contain numbers.
				|| tt >= 10 )
			{
				tokens[tokenno].text[tokpl-1] = 0;
				if( currenttokenype != 1 )
					tokenno++;
				tokens = realloc( tokens, (tokenno+1) * sizeof( struct tokes ) );
				tokpl = 1;
				tokens[tokenno].text = malloc( tokpl + 2 );
				tokens[tokenno].text[0] = c;
				tokens[tokenno].text[1] = 0;
				tokens[tokenno].lineno = lineno;
				tokens[tokenno].charno = charno;
				tokens[tokenno].type = tt;
				currenttokenype = tt;
			}
		}
	}
	tokenno++;
	
	fprintf( stderr, "Parsing ok.\n" );
	numtoks = tokenno;
	tokenno = 0;
	Block();
	
	fprintf( stderr, "Ok.\n" );
}
