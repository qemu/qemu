// SPDX-License-Identifier: MIT or Apache-2.0 or GPL-2.0-or-later

// shadowing is useful together with "if let"
#![allow(clippy::shadow_unrelated)]

use proc_macro2::{
    Delimiter, Group, Ident, Punct, Spacing, Span, TokenStream, TokenTree, TokenTree as TT,
};
use syn::Error;

pub struct BitsConstInternal {
    typ: TokenTree,
}

fn paren(ts: TokenStream) -> TokenTree {
    TT::Group(Group::new(Delimiter::Parenthesis, ts))
}

fn ident(s: &'static str) -> TokenTree {
    TT::Ident(Ident::new(s, Span::call_site()))
}

fn punct(ch: char) -> TokenTree {
    TT::Punct(Punct::new(ch, Spacing::Alone))
}

/// Implements a recursive-descent parser that translates Boolean expressions on
/// bitmasks to invocations of `const` functions defined by the `bits!` macro.
impl BitsConstInternal {
    // primary ::= '(' or ')'
    //           | ident
    //           | '!' ident
    fn parse_primary(
        &self,
        tok: TokenTree,
        it: &mut dyn Iterator<Item = TokenTree>,
        out: &mut TokenStream,
    ) -> Result<Option<TokenTree>, Error> {
        let next = match tok {
            TT::Group(ref g) => {
                if g.delimiter() != Delimiter::Parenthesis && g.delimiter() != Delimiter::None {
                    return Err(Error::new(g.span(), "expected parenthesis"));
                }
                let mut stream = g.stream().into_iter();
                let Some(first_tok) = stream.next() else {
                    return Err(Error::new(g.span(), "expected operand, found ')'"));
                };
                let mut output = TokenStream::new();
                // start from the lowest precedence
                let next = self.parse_or(first_tok, &mut stream, &mut output)?;
                if let Some(tok) = next {
                    return Err(Error::new(tok.span(), format!("unexpected token {tok}")));
                }
                out.extend(Some(paren(output)));
                it.next()
            }
            TT::Ident(_) => {
                let mut output = TokenStream::new();
                output.extend([
                    self.typ.clone(),
                    TT::Punct(Punct::new(':', Spacing::Joint)),
                    TT::Punct(Punct::new(':', Spacing::Joint)),
                    tok,
                ]);
                out.extend(Some(paren(output)));
                it.next()
            }
            TT::Punct(ref p) => {
                if p.as_char() != '!' {
                    return Err(Error::new(p.span(), "expected operand"));
                }
                let Some(rhs_tok) = it.next() else {
                    return Err(Error::new(p.span(), "expected operand at end of input"));
                };
                let next = self.parse_primary(rhs_tok, it, out)?;
                out.extend([punct('.'), ident("invert"), paren(TokenStream::new())]);
                next
            }
            _ => {
                return Err(Error::new(tok.span(), "unexpected literal"));
            }
        };
        Ok(next)
    }

    fn parse_binop<
        F: Fn(
            &Self,
            TokenTree,
            &mut dyn Iterator<Item = TokenTree>,
            &mut TokenStream,
        ) -> Result<Option<TokenTree>, Error>,
    >(
        &self,
        tok: TokenTree,
        it: &mut dyn Iterator<Item = TokenTree>,
        out: &mut TokenStream,
        ch: char,
        f: F,
        method: &'static str,
    ) -> Result<Option<TokenTree>, Error> {
        let mut next = f(self, tok, it, out)?;
        while next.is_some() {
            let op = next.as_ref().unwrap();
            let TT::Punct(ref p) = op else { break };
            if p.as_char() != ch {
                break;
            }

            let Some(rhs_tok) = it.next() else {
                return Err(Error::new(p.span(), "expected operand at end of input"));
            };
            let mut rhs = TokenStream::new();
            next = f(self, rhs_tok, it, &mut rhs)?;
            out.extend([punct('.'), ident(method), paren(rhs)]);
        }
        Ok(next)
    }

    // sub ::= primary ('-' primary)*
    pub fn parse_sub(
        &self,
        tok: TokenTree,
        it: &mut dyn Iterator<Item = TokenTree>,
        out: &mut TokenStream,
    ) -> Result<Option<TokenTree>, Error> {
        self.parse_binop(tok, it, out, '-', Self::parse_primary, "difference")
    }

    // and ::= sub ('&' sub)*
    fn parse_and(
        &self,
        tok: TokenTree,
        it: &mut dyn Iterator<Item = TokenTree>,
        out: &mut TokenStream,
    ) -> Result<Option<TokenTree>, Error> {
        self.parse_binop(tok, it, out, '&', Self::parse_sub, "intersection")
    }

    // xor ::= and ('&' and)*
    fn parse_xor(
        &self,
        tok: TokenTree,
        it: &mut dyn Iterator<Item = TokenTree>,
        out: &mut TokenStream,
    ) -> Result<Option<TokenTree>, Error> {
        self.parse_binop(tok, it, out, '^', Self::parse_and, "symmetric_difference")
    }

    // or ::= xor ('|' xor)*
    pub fn parse_or(
        &self,
        tok: TokenTree,
        it: &mut dyn Iterator<Item = TokenTree>,
        out: &mut TokenStream,
    ) -> Result<Option<TokenTree>, Error> {
        self.parse_binop(tok, it, out, '|', Self::parse_xor, "union")
    }

    pub fn parse(
        it: &mut dyn Iterator<Item = TokenTree>,
    ) -> Result<proc_macro2::TokenStream, Error> {
        let mut pos = Span::call_site();
        let mut typ = proc_macro2::TokenStream::new();

        // Gobble everything up to an `@` sign, which is followed by a
        // parenthesized expression; that is, all token trees except the
        // last two form the type.
        let next = loop {
            let tok = it.next();
            if let Some(ref t) = tok {
                pos = t.span();
            }
            match tok {
                None => break None,
                Some(TT::Punct(ref p)) if p.as_char() == '@' => {
                    let tok = it.next();
                    if let Some(ref t) = tok {
                        pos = t.span();
                    }
                    break tok;
                }
                Some(x) => typ.extend(Some(x)),
            }
        };

        let Some(tok) = next else {
            return Err(Error::new(
                pos,
                "expected expression, do not call this macro directly",
            ));
        };
        let TT::Group(ref _group) = tok else {
            return Err(Error::new(
                tok.span(),
                "expected parenthesis, do not call this macro directly",
            ));
        };
        let mut out = TokenStream::new();
        let state = Self {
            typ: TT::Group(Group::new(Delimiter::None, typ)),
        };

        let next = state.parse_primary(tok, it, &mut out)?;

        // A parenthesized expression is a single production of the grammar,
        // so the input must have reached the last token.
        if let Some(tok) = next {
            return Err(Error::new(tok.span(), format!("unexpected token {tok}")));
        }
        Ok(out)
    }
}
