/* Support for generating ACPI TPM tables
 *
 * Copyright (C) 2018 IBM, Corp.
 * Copyright (C) 2018 Red Hat Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/acpi/tpm.h"

void tpm_build_ppi_acpi(TPMIf *tpm, Aml *dev)
{
    Aml *method, *field, *ifctx, *ifctx2, *ifctx3, *func_mask,
        *not_implemented, *pak, *tpm2, *tpm3, *pprm, *pprq, *zero, *one;

    if (!object_property_get_bool(OBJECT(tpm), "ppi", &error_abort)) {
        return;
    }

    zero = aml_int(0);
    one = aml_int(1);
    func_mask = aml_int(TPM_PPI_FUNC_MASK);
    not_implemented = aml_int(TPM_PPI_FUNC_NOT_IMPLEMENTED);

    /*
     * TPP2 is for the registers that ACPI code used to pass
     * the PPI code and parameter (PPRQ, PPRM) to the firmware.
     */
    aml_append(dev,
               aml_operation_region("TPP2", AML_SYSTEM_MEMORY,
                                    aml_int(TPM_PPI_ADDR_BASE + 0x100),
                                    0x5A));
    field = aml_field("TPP2", AML_ANY_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("PPIN", 8));
    aml_append(field, aml_named_field("PPIP", 32));
    aml_append(field, aml_named_field("PPRP", 32));
    aml_append(field, aml_named_field("PPRQ", 32));
    aml_append(field, aml_named_field("PPRM", 32));
    aml_append(field, aml_named_field("LPPR", 32));
    aml_append(dev, field);
    pprq = aml_name("PPRQ");
    pprm = aml_name("PPRM");

    aml_append(dev,
               aml_operation_region(
                   "TPP3", AML_SYSTEM_MEMORY,
                   aml_int(TPM_PPI_ADDR_BASE +
                           0x15a /* movv, docs/specs/tpm.rst */),
                           0x1));
    field = aml_field("TPP3", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("MOVV", 8));
    aml_append(dev, field);

    /*
     * DerefOf in Windows is broken with SYSTEM_MEMORY.  Use a dynamic
     * operation region inside of a method for getting FUNC[op].
     */
    method = aml_method("TPFN", 1, AML_SERIALIZED);
    {
        Aml *op = aml_arg(0);
        ifctx = aml_if(aml_lgreater_equal(op, aml_int(0x100)));
        {
            aml_append(ifctx, aml_return(zero));
        }
        aml_append(method, ifctx);

        aml_append(method,
            aml_operation_region("TPP1", AML_SYSTEM_MEMORY,
                aml_add(aml_int(TPM_PPI_ADDR_BASE), op, NULL), 0x1));
        field = aml_field("TPP1", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
        aml_append(field, aml_named_field("TPPF", 8));
        aml_append(method, field);
        aml_append(method, aml_return(aml_name("TPPF")));
    }
    aml_append(dev, method);

    /*
     * Use global TPM2 & TPM3 variables to workaround Windows ACPI bug
     * when returning packages.
     */
    pak = aml_package(2);
    aml_append(pak, zero);
    aml_append(pak, zero);
    aml_append(dev, aml_name_decl("TPM2", pak));
    tpm2 = aml_name("TPM2");

    pak = aml_package(3);
    aml_append(pak, zero);
    aml_append(pak, zero);
    aml_append(pak, zero);
    aml_append(dev, aml_name_decl("TPM3", pak));
    tpm3 = aml_name("TPM3");

    method = aml_method("_DSM", 4, AML_SERIALIZED);
    {
        uint8_t zerobyte[1] = { 0 };
        Aml *function, *arguments, *rev, *op, *op_arg, *op_flags, *uuid;

        uuid = aml_arg(0);
        rev = aml_arg(1);
        function = aml_arg(2);
        arguments = aml_arg(3);
        op = aml_local(0);
        op_flags = aml_local(1);

        /* Physical Presence Interface */
        ifctx = aml_if(
            aml_equal(uuid,
                      aml_touuid("3DDDFAA6-361B-4EB4-A424-8D10089D1653")));
        {
            /* standard DSM query function */
            ifctx2 = aml_if(aml_equal(function, zero));
            {
                uint8_t byte_list[2] = { 0xff, 0x01 }; /* functions 1-8 */

                aml_append(ifctx2,
                           aml_return(aml_buffer(sizeof(byte_list),
                                                 byte_list)));
            }
            aml_append(ifctx, ifctx2);

            /*
             * PPI 1.0: 2.1.1 Get Physical Presence Interface Version
             *
             * Arg 2 (Integer): Function Index = 1
             * Arg 3 (Package): Arguments = Empty Package
             * Returns: Type: String
             */
            ifctx2 = aml_if(aml_equal(function, one));
            {
                aml_append(ifctx2, aml_return(aml_string("1.3")));
            }
            aml_append(ifctx, ifctx2);

            /*
             * PPI 1.0: 2.1.3 Submit TPM Operation Request to Pre-OS Environment
             *
             * Arg 2 (Integer): Function Index = 2
             * Arg 3 (Package): Arguments = Package: Type: Integer
             *                              Operation Value of the Request
             * Returns: Type: Integer
             *          0: Success
             *          1: Operation Value of the Request Not Supported
             *          2: General Failure
             */
            ifctx2 = aml_if(aml_equal(function, aml_int(2)));
            {
                /* get opcode */
                aml_append(ifctx2,
                           aml_store(aml_derefof(aml_index(arguments,
                                                           zero)), op));

                /* get opcode flags */
                aml_append(ifctx2,
                           aml_store(aml_call1("TPFN", op), op_flags));

                /* if func[opcode] & TPM_PPI_FUNC_NOT_IMPLEMENTED */
                ifctx3 = aml_if(
                    aml_equal(
                        aml_and(op_flags, func_mask, NULL),
                        not_implemented));
                {
                    /* 1: Operation Value of the Request Not Supported */
                    aml_append(ifctx3, aml_return(one));
                }
                aml_append(ifctx2, ifctx3);

                aml_append(ifctx2, aml_store(op, pprq));
                aml_append(ifctx2, aml_store(zero, pprm));
                /* 0: success */
                aml_append(ifctx2, aml_return(zero));
            }
            aml_append(ifctx, ifctx2);

            /*
             * PPI 1.0: 2.1.4 Get Pending TPM Operation Requested By the OS
             *
             * Arg 2 (Integer): Function Index = 3
             * Arg 3 (Package): Arguments = Empty Package
             * Returns: Type: Package of Integers
             *          Integer 1: Function Return code
             *                     0: Success
             *                     1: General Failure
             *          Integer 2: Pending operation requested by the OS
             *                     0: None
             *                    >0: Operation Value of the Pending Request
             *          Integer 3: Optional argument to pending operation
             *                     requested by the OS
             *                     0: None
             *                    >0: Argument Value of the Pending Request
             */
            ifctx2 = aml_if(aml_equal(function, aml_int(3)));
            {
                /*
                 * Revision ID of 1, no integer parameter beyond
                 * parameter two are expected
                 */
                ifctx3 = aml_if(aml_equal(rev, one));
                {
                    /* TPM2[1] = PPRQ */
                    aml_append(ifctx3,
                               aml_store(pprq, aml_index(tpm2, one)));
                    aml_append(ifctx3, aml_return(tpm2));
                }
                aml_append(ifctx2, ifctx3);

                /*
                 * A return value of {0, 23, 1} indicates that
                 * operation 23 with argument 1 is pending.
                 */
                ifctx3 = aml_if(aml_equal(rev, aml_int(2)));
                {
                    /* TPM3[1] = PPRQ */
                    aml_append(ifctx3,
                               aml_store(pprq, aml_index(tpm3, one)));
                    /* TPM3[2] = PPRM */
                    aml_append(ifctx3,
                               aml_store(pprm, aml_index(tpm3, aml_int(2))));
                    aml_append(ifctx3, aml_return(tpm3));
                }
                aml_append(ifctx2, ifctx3);
            }
            aml_append(ifctx, ifctx2);

            /*
             * PPI 1.0: 2.1.5 Get Platform-Specific Action to Transition to
             *     Pre-OS Environment
             *
             * Arg 2 (Integer): Function Index = 4
             * Arg 3 (Package): Arguments = Empty Package
             * Returns: Type: Integer
             *          0: None
             *          1: Shutdown
             *          2: Reboot
             *          3: OS Vendor-specific
             */
            ifctx2 = aml_if(aml_equal(function, aml_int(4)));
            {
                /* reboot */
                aml_append(ifctx2, aml_return(aml_int(2)));
            }
            aml_append(ifctx, ifctx2);

            /*
             * PPI 1.0: 2.1.6 Return TPM Operation Response to OS Environment
             *
             * Arg 2 (Integer): Function Index = 5
             * Arg 3 (Package): Arguments = Empty Package
             * Returns: Type: Package of Integer
             *          Integer 1: Function Return code
             *                     0: Success
             *                     1: General Failure
             *          Integer 2: Most recent operation request
             *                     0: None
             *                    >0: Operation Value of the most recent request
             *          Integer 3: Response to the most recent operation request
             *                     0: Success
             *                     0x00000001..0x00000FFF: Corresponding TPM
             *                                             error code
             *                     0xFFFFFFF0: User Abort or timeout of dialog
             *                     0xFFFFFFF1: firmware Failure
             */
            ifctx2 = aml_if(aml_equal(function, aml_int(5)));
            {
                /* TPM3[1] = LPPR */
                aml_append(ifctx2,
                           aml_store(aml_name("LPPR"),
                                     aml_index(tpm3, one)));
                /* TPM3[2] = PPRP */
                aml_append(ifctx2,
                           aml_store(aml_name("PPRP"),
                                     aml_index(tpm3, aml_int(2))));
                aml_append(ifctx2, aml_return(tpm3));
            }
            aml_append(ifctx, ifctx2);

            /*
             * PPI 1.0: 2.1.7 Submit preferred user language
             *
             * Arg 2 (Integer): Function Index = 6
             * Arg 3 (Package): Arguments = String Package
             *                  Preferred language code
             * Returns: Type: Integer
             * Function Return Code
             *          3: Not implemented
             */
            ifctx2 = aml_if(aml_equal(function, aml_int(6)));
            {
                /* 3 = not implemented */
                aml_append(ifctx2, aml_return(aml_int(3)));
            }
            aml_append(ifctx, ifctx2);

            /*
             * PPI 1.1: 2.1.7 Submit TPM Operation Request to
             *     Pre-OS Environment 2
             *
             * Arg 2 (Integer): Function Index = 7
             * Arg 3 (Package): Arguments = Package: Type: Integer
             *                  Integer 1: Operation Value of the Request
             *                  Integer 2: Argument for Operation (optional)
             * Returns: Type: Integer
             *          0: Success
             *          1: Not Implemented
             *          2: General Failure
             *          3: Operation blocked by current firmware settings
             */
            ifctx2 = aml_if(aml_equal(function, aml_int(7)));
            {
                /* get opcode */
                aml_append(ifctx2, aml_store(aml_derefof(aml_index(arguments,
                                                                   zero)),
                                             op));

                /* get opcode flags */
                aml_append(ifctx2, aml_store(aml_call1("TPFN", op),
                                             op_flags));
                /* if func[opcode] & TPM_PPI_FUNC_NOT_IMPLEMENTED */
                ifctx3 = aml_if(
                    aml_equal(
                        aml_and(op_flags, func_mask, NULL),
                        not_implemented));
                {
                    /* 1: not implemented */
                    aml_append(ifctx3, aml_return(one));
                }
                aml_append(ifctx2, ifctx3);

                /* if func[opcode] & TPM_PPI_FUNC_BLOCKED */
                ifctx3 = aml_if(
                    aml_equal(
                        aml_and(op_flags, func_mask, NULL),
                        aml_int(TPM_PPI_FUNC_BLOCKED)));
                {
                    /* 3: blocked by firmware */
                    aml_append(ifctx3, aml_return(aml_int(3)));
                }
                aml_append(ifctx2, ifctx3);

                /* revision to integer */
                ifctx3 = aml_if(aml_equal(rev, one));
                {
                    /* revision 1 */
                    /* PPRQ = op */
                    aml_append(ifctx3, aml_store(op, pprq));
                    /* no argument, PPRM = 0 */
                    aml_append(ifctx3, aml_store(zero, pprm));
                }
                aml_append(ifctx2, ifctx3);

                ifctx3 = aml_if(aml_equal(rev, aml_int(2)));
                {
                    /* revision 2 */
                    /* PPRQ = op */
                    op_arg = aml_derefof(aml_index(arguments, one));
                    aml_append(ifctx3, aml_store(op, pprq));
                    /* PPRM = arg3[1] */
                    aml_append(ifctx3, aml_store(op_arg, pprm));
                }
                aml_append(ifctx2, ifctx3);
                /* 0: success */
                aml_append(ifctx2, aml_return(zero));
            }
            aml_append(ifctx, ifctx2);

            /*
             * PPI 1.1: 2.1.8 Get User Confirmation Status for Operation
             *
             * Arg 2 (Integer): Function Index = 8
             * Arg 3 (Package): Arguments = Package: Type: Integer
             *                  Operation Value that may need user confirmation
             * Returns: Type: Integer
             *          0: Not implemented
             *          1: Firmware only
             *          2: Blocked for OS by firmware configuration
             *          3: Allowed and physically present user required
             *          4: Allowed and physically present user not required
             */
            ifctx2 = aml_if(aml_equal(function, aml_int(8)));
            {
                /* get opcode */
                aml_append(ifctx2,
                           aml_store(aml_derefof(aml_index(arguments,
                                                           zero)),
                                     op));

                /* get opcode flags */
                aml_append(ifctx2, aml_store(aml_call1("TPFN", op),
                                             op_flags));
                /* return confirmation status code */
                aml_append(ifctx2,
                           aml_return(
                               aml_and(op_flags, func_mask, NULL)));
            }
            aml_append(ifctx, ifctx2);

            aml_append(ifctx, aml_return(aml_buffer(1, zerobyte)));
        }
        aml_append(method, ifctx);

        /*
         * "TCG Platform Reset Attack Mitigation Specification 1.00",
         * Chapter 6 "ACPI _DSM Function"
         */
        ifctx = aml_if(
            aml_equal(uuid,
                      aml_touuid("376054ED-CC13-4675-901C-4756D7F2D45D")));
        {
            /* standard DSM query function */
            ifctx2 = aml_if(aml_equal(function, zero));
            {
                uint8_t byte_list[1] = { 0x03 }; /* functions 1-2 supported */

                aml_append(ifctx2,
                           aml_return(aml_buffer(sizeof(byte_list),
                                                 byte_list)));
            }
            aml_append(ifctx, ifctx2);

            /*
             * TCG Platform Reset Attack Mitigation Specification 1.0 Ch.6
             *
             * Arg 2 (Integer): Function Index = 1
             * Arg 3 (Package): Arguments = Package: Type: Integer
             *                  Operation Value of the Request
             * Returns: Type: Integer
             *          0: Success
             *          1: General Failure
             */
            ifctx2 = aml_if(aml_equal(function, one));
            {
                aml_append(ifctx2,
                           aml_store(aml_derefof(aml_index(arguments, zero)),
                                     op));
                {
                    aml_append(ifctx2, aml_store(op, aml_name("MOVV")));

                    /* 0: success */
                    aml_append(ifctx2, aml_return(zero));
                }
            }
            aml_append(ifctx, ifctx2);
        }
        aml_append(method, ifctx);
    }
    aml_append(dev, method);
}
