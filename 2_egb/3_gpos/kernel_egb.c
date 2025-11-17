#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/serdev.h>
#include <linux/fs.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

// Autor del modulo
#define AUTHOR              "EGB_GRUPO_2"
// Nombre del char device
#define CHRDEV_NAME         "EGB"
// Minor number del char device
#define CHRDEV_MINOR        1
// Cantidad de char devices
#define CHRDEV_COUNT        1
// Cantidad de caracteres maximos en el buffer
#define SHARED_BUFFER_SIZE  64
// Baud rate del UART
#define BAUD_RATE           115200
// Paridad
#define PARITY              SERDEV_PARITY_NONE

#define MSG_1 "get_sp"
#define MSG_2 "set_sp"
#define MSG_3 "get_err"
#define MSG_4 "set_err"
#define MSG_5 "set_mode"
#define MSG_6 "log"
#define MSG_7 "stop"
#define MSG_8 "start"

// Variable que guarda los major y minor numbers del char device
static dev_t chrdev_number;
// Variable que representa el char device
static struct cdev chrdev;
// Clase del char device
static struct class *chrdev_class;
// ID
static struct of_device_id serdev_ids[] = {
	{ .compatible = "brightlight,td3_uart", },
	{ }
};
MODULE_DEVICE_TABLE(of, serdev_ids);

static int received = 0, received_size = 0, writing_uart = 0;
static wait_queue_head_t waitqueue;

static char shared_buffer[SHARED_BUFFER_SIZE];
static char tx_buffer[SHARED_BUFFER_SIZE];
static char rx_buffer[SHARED_BUFFER_SIZE];
// Puntero UART
static struct serdev_device *g_serdev = NULL;

// Callbacks de fops
static ssize_t chr_dev_read(struct file *f, char __user *buff, size_t size, loff_t *off);
static ssize_t chr_dev_write(struct file *f, const char __user *buff, size_t size, loff_t *off);
// Callbacks del driver uart 
static int uart_probe(struct serdev_device *serdev);
static void uart_remove(struct serdev_device *serdev);
// Operaciones del UART
static size_t uart_recv(struct serdev_device *serdev, const unsigned char *buffer, size_t size);

// Operaciones de archivos del char device
static struct file_operations chrdev_ops = {
    .owner = THIS_MODULE,
    .read = chr_dev_read,
    .write = chr_dev_write
};
// Operaciones del UART
static const struct serdev_device_ops egb_uart_ops = {
    .receive_buf = uart_recv,
};

/**
 * @brief Llamada cuando recibo dato por UART
 */
static size_t uart_recv(struct serdev_device *serdev, const unsigned char *buffer, size_t size) {
    printk(KERN_INFO "%s: ENTRE A UART_RECV\n", AUTHOR);

    if(size>3){
        int to_copy = min(size, SHARED_BUFFER_SIZE - 1); //cantidad de datos a copiar
        memcpy(shared_buffer, buffer, to_copy); //Copio to_copy cantidad de elementos de buf a shrd_buf
        shared_buffer[to_copy] = '\0';
        received_size = to_copy;
        received = 1;
        printk(KERN_INFO "%s: data cant %zu ---- Recibido por UART: '%s'\n", AUTHOR,size, shared_buffer);
        wake_up_interruptible(&waitqueue);
    }
    else{
        printk(KERN_ERR "%s: Recibi basura por UART\n", AUTHOR);
    }
    return size;
}
/**
 * @brief Operacion si se lee el char device
 */
static ssize_t chr_dev_read(struct file *f, char __user *buff, size_t size, loff_t *off) {
    printk(KERN_INFO "%s: ENTRE A CHR_DEV_READ\n", AUTHOR);
    int not_copied;
    if(*off > 0) {
        printk(KERN_INFO "%s: Lectura de char device terminada\n", AUTHOR);
	    return 0;
    }
    // Bloqueo hasta recibir dato por UART
    wait_event_interruptible(waitqueue, received == 1);
    not_copied = copy_to_user(buff, shared_buffer, received_size);
    *off = received_size - not_copied;
    
    received = 0;
    printk(KERN_INFO "%s: Leido del char device '%s'\n", AUTHOR, shared_buffer);
    return received_size - not_copied;
}

/**
 * @brief Operacion si se escribe el char device
 */
static ssize_t chr_dev_write(struct file *f, const char __user *buff, size_t size, loff_t *off) {
    printk(KERN_INFO "%s: ENTRE A CHR_DEV_WRITE\n", AUTHOR);
    static int to_copy, not_copied, copied;
    static char buff_to_print[SHARED_BUFFER_SIZE];
    
    static int count=0;
    count++;
    to_copy = min(size, sizeof(shared_buffer)-1);
    not_copied = copy_from_user(shared_buffer, buff, to_copy);
    //Longitud actual
    copied = to_copy-not_copied;
    printk("%s: To copy: %d Not Copied: %d \n",AUTHOR,to_copy,not_copied);
    //Uso buffer aparte para imprimir string
    memcpy(buff_to_print,shared_buffer,copied);
    buff_to_print[copied] = '\0';
    //Remuevo \n del string si es que existe
    if(buff_to_print[copied-1]=='\n')buff_to_print[copied-1]='\0';

    printk("%s: Escrito sobre /dev/%s - %s\n", AUTHOR, CHRDEV_NAME, buff_to_print);
    // UART
    if(g_serdev != NULL){
        // Se envia al UART
        printk("%s: DATA ENVIADA POR UART!!!!!!!!!\n",AUTHOR);
        serdev_device_write_buf(g_serdev, shared_buffer, copied);
        // Se devuelve cuanto se copio
        return copied;
    }
    // Retorna 0 si no hay UART
    return copied;
}

/**
 * @brief Operacion si se detecta UART. Crea el serdev device y le asigna las operaciones
 * @return Devuelve cero si la inicializacion fue correcta
 */
static int uart_probe(struct serdev_device *serdev) {
    printk(KERN_INFO "%s: Se conecto UART\n", AUTHOR);
    serdev_device_set_client_ops(serdev, &egb_uart_ops);
    // Se intenta abrir el UART
    if(serdev_device_open(serdev)) {
        printk(KERN_ERR "%s: Error abriendo el UART\n", AUTHOR);
        return -1;
    }
    // Configuracion de UART
    serdev_device_set_baudrate(serdev, 115200);
    serdev_device_set_flow_control(serdev, false);
    serdev_device_set_parity(serdev, PARITY);
    g_serdev = serdev;
    if(g_serdev == NULL) {
        printk(KERN_ERR "%s: Error configurando el UART\n", AUTHOR);
        return -1;
    }
    printk(KERN_INFO "%s: Config UART completada exitosamente\n", AUTHOR);
    return 0;
}

/**
 * @brief Operacion si se remueve UART. Cierra el serdev device.
 */
static void uart_remove(struct serdev_device *serdev) {
    printk(KERN_INFO "%s: UART cerrada\n", AUTHOR);
    // Se cierra el UART
    serdev_device_close(serdev);
}

// Operaciones del driver uart
static struct serdev_device_driver egb_uart_driver = {
    .probe = uart_probe,
    .remove = uart_remove,
    .driver = {
        .name = "DRIVER_UART",
        .of_match_table = serdev_ids,
    }
};

/**
 * @brief Crea el char device
 * @return Devuelve cero si la inicializacion fue correcta
 */
static int __init module_kernel_init(void) {
    init_waitqueue_head(&waitqueue);
    // Reservar char device
    if(alloc_chrdev_region(&chrdev_number, CHRDEV_MINOR, CHRDEV_COUNT, AUTHOR) < 0) {
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }
    // Mensaje para buscar el char device
    printk(KERN_INFO "%s: Se reservo char device con major %d y minor %d\n", AUTHOR, MAJOR(chrdev_number), MINOR(chrdev_number));
    // Inicializa el char device y sus operaciones de archivos
    cdev_init(&chrdev, &chrdev_ops);
    // Asocia el char device a la zona reservada
    if(cdev_add(&chrdev, chrdev_number, CHRDEV_COUNT) < 0) {
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }
    // Crea la estructura de clase
    chrdev_class = class_create(AUTHOR);
    // Verifica error
    if(IS_ERR(chrdev_class)) {
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }
    // Se crea el archivo del char device
    if(IS_ERR(device_create(chrdev_class, NULL, chrdev_number, NULL, AUTHOR))) {
        class_destroy(chrdev_class);
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }
    // Registro driver para UART
    if(serdev_device_driver_register(&egb_uart_driver)) {
        printk(KERN_ERR "%s: No se pudo crear el driver de UART\n", AUTHOR);
        return -1;
    }
    // Mensaje de correcta finalizacion
    printk(KERN_INFO "%s: Fue creado el char device y driver UART\n", AUTHOR);
    return 0;
}

/**
 * @brief Libera el espacio reservado del char device
 */
static void __exit module_kernel_exit(void) {
    device_destroy(chrdev_class, chrdev_number);
    class_destroy(chrdev_class);
    unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
    cdev_del(&chrdev);
    serdev_device_driver_unregister(&egb_uart_driver);
    printk(KERN_INFO "%s: Modulo removido\n", AUTHOR);
}

// Funciones de inicializacion y salida
module_init(module_kernel_init);
module_exit(module_kernel_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Modulo de kernel EGB - GRUPO 2");