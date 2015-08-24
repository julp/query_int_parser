/* ! > & > | */

NODE(T_INVALID, "<invalid>", "", NULL, NULL) /* fake, must be first */
OPERATOR(T_OR, "or", "|", NULL, eval_or, ASSOC_LEFT, 1, BINARY)
#ifdef WITH_EXTRA_XOR
OPERATOR(T_XOR, "xor", "^", NULL, eval_xor, ASSOC_LEFT, 2, BINARY)
#endif /* WITH_EXTRA_XOR */
OPERATOR(T_AND, "and", "&", NULL, eval_and, ASSOC_LEFT, 3, BINARY)
OPERATOR(T_NOT, "not", "!", NULL, eval_not, ASSOC_RIGHT, 4, UNARY)
NODE(T_LPAREN, "(", "(", NULL, NULL)
NODE(T_RPAREN, ")", ")", NULL, NULL)
NODE(T_SYMBOL, "<symbol>", "123456789", parse_int_symbol, eval_int)
NODE(T_IGNORABLES, "<ignorables>", " ", NULL, NULL)
